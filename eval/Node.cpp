// Copyright (C) 2013 Michael Biggs.  See the COPYING file at the top-level
// directory of this distribution and at http://shok.io/code/copyright.html

#include "Node.h"

#include "Block.h"
#include "Brace.h"
#include "Command.h"
#include "CommandFragment.h"
#include "EvalError.h"
#include "Expression.h"
#include "Identifier.h"
#include "IsVar.h"
#include "Log.h"
#include "New.h"
#include "NewInit.h"
#include "Operator.h"
#include "OperatorParser.h"
#include "ProcCall.h"
#include "RootNode.h"
#include "TypeSpec.h"
#include "Variable.h"

#include <boost/lexical_cast.hpp>

#include <string>
using std::string;

using namespace eval;

/* Statics */

Node* Node::MakeNode(Log& log, RootNode*const root, const Token& t) {
  if ("[" == t.name)
    return new Command(log, root, t);
  if ("(" == t.name)
    return new Brace(log, root, t, true);
  if ("{" == t.name)
    return new Block(log, root, t);
  if ("]" == t.name ||
      ")" == t.name ||
      "}" == t.name)
    return new Brace(log, root, t, false);
  if ("cmd" == t.name)
    return new CommandFragment(log, root, t);
  if ("ID" == t.name)
    return new Identifier(log, root, t);
  if ("var" == t.name)
    return new Variable(log, root, t);
  if ("PLUS" == t.name ||
      "MINUS" == t.name ||
      "STAR" == t.name ||
      "SLASH" == t.name ||
      "PERCENT" == t.name ||
      "CARAT" == t.name ||
      "PIPE" == t.name ||
      "AMP" == t.name ||
      "TILDE" == t.name ||
      "DOUBLETILDE" == t.name)
    return new Operator(log, root, t);
  if ("exp" == t.name)
    return new Expression(log, root, t);
  if ("new" == t.name)
    return new New(log, root, t);
  if ("init" == t.name)
    return new NewInit(log, root, t);
  if ("type" == t.name)
    return new TypeSpec(log, root, t);
  if ("call" == t.name)
    return new ProcCall(log, root, t);
  if ("isvar" == t.name)
    return new IsVar(log, root, t);
  throw EvalError("Unsupported token " + t.print());
  return NULL;    // guard
}

Node* Node::InsertNode(Log& log, Node* current, Node* n) {
  if (!current || !n) {
    throw EvalError("NULL nodes provided to Node::InsertNode()");
  }
  Brace* brace = dynamic_cast<Brace*>(n);

  // Neither an open nor a closing brace; add as a child of current
  if (!brace) {
    n->initScopeNode(current);
    OperatorParser* oP = dynamic_cast<OperatorParser*>(current->children.at(0));
    if (oP) {
      oP->insertNode(n);
    } else {
      n->parent = current;
      current->addChild(n);
    }

    // Some nodes employ Pratt parsing, which allows children nodes to be
    // setup() before the parent is complete.  Check if that's the case here.

    // If current's first child is an Expression or TypeSpec:
    //  - don't insert the current node
    //  - instead, call InsertExpressionNode() which we implement here on Node
    //  - that uses a ... static stack.... uhhhhh..... sure?  and tracks all
    //  the nodes and makes them a tree
    //  - when we hit the closing brace of current, we FinalizeExpressionNode()
    //  or some such.  This pulls out the tree, giving us the root, which
    //  becomes the single child of the Expression (the first child of
    //  current).

    // That should juuuuust... work.

    // Note that InsertNode will recurse into nodes (say, vars, and functions)
    // that are alongside the Expression.  We need to be quite stateless about
    // when we are PrattParsing something, and multiple of these PrattParsers
    // can be on-the-go at the same time.

    // Let's find a way to encapsulate the Expression's PrattParsingness and
    // shove it onto that Exp/TypeSpec node.

    // Within the Pratt parsing, anytime we finish up an operator, perform the
    // operator's setupAsParent() or whatever.  Actually, BinaryOperators might
    // have to be craaazy, with a setupLeft() and setupRight().  During
    // setupLeft(), an operator (*ahem* like &) may determine something about
    // its left branch that any random thing that happens on its right side
    // might need to know.  So it can shove this info into the PrattParsingness
    // object, and this data should get passed down the initNode() of
    // everything we parse until we bubble up and finally call the setupRight()
    // on this operator (followed by its own setupNode(), naturally!).

    // wheeeeeee!

    // What if we don't know about Exp/TS, we just notice when we find our
    // first Operator at some level, whoah, Pratt parse this guy!  hmm..

    return current;   // stay
  }

  // Open brace: descend into current; new nodes will be its children
  if (brace->isOpen()) {
    n->parent = current;
    current->addChild(n);
    n->initScopeNode(current);
    return n;         // descend
  }

  // Closing brace: ensure it matches with the open brace (current), then
  // ascend our focus up.  Perform static analysis on the new parent.
  //
  // When parentheses are matched, they will be eliminated from the AST since
  // they represent nothing.  Instead, their first child (operator) will take
  // over the "parent" spot; its children (operands) will remain as the
  // operator's children.
  // current should be the open brace/paren to match against
  if (!current->parent) {
    throw EvalError("Cannot move above root node " + current->name);
  }

  Brace* open = dynamic_cast<Brace*>(current);
  if (!open) {
    throw EvalError("Found closing brace " + brace->name + " but its parent " + current->name + " is not an open brace");
  }
  if (!open->matchesCloseBrace(brace)) {
    throw EvalError("Incorrect brace/paren match: '" + open->name + "' against '" + n->name + "'");
  }
  Node* parent = current->parent;

  // Parentheses: these are now useless.  We promote the first child (there
  // must be at least one child!) into the parent (paren) spot; it is the
  // operator, its children are its operands.  Huzzah!
  if (open->isIrrelevant()) {
    // Extract the first child of the open brace; it is the new "operator"
    if (open->children.size() < 1) {
      throw EvalError("Empty parens in the AST are not allowed");
    }
    Node* op = open->children.front();    // "operator" becomes the parent
    open->children.pop_front();
    op->parent = open->parent;
    // a '({' cannot appear in the input.  Note that actual parentheses in
    // expressions are not given to us as a bare '(' brace.
    if (op->children.size() != 0) {
      throw EvalError("Cannot escalate child " + op->name + " that has " + boost::lexical_cast<string>(op->children.size()) + " > 0 children");
    }
    op->children = open->children;
    open->children.clear();   // Clear open's children so they're not deleted
    // Replace op's children's parent links from open to op
    for (Node::child_iter i = op->children.begin();
         i != op->children.end(); ++i) {
      (*i)->parent = op;
    }
    // Replace parent's child of 'open' with 'op'
    for (Node::child_mod_iter i = op->parent->children.begin();
         i != op->parent->children.end(); ++i) {
      if (*i == open) {
        *i = op;
        break;    // a node must only appear once in the AST
      }
    }
    delete open;
    // Errors from setupAsParent are recoverable
    try {
      op->setupAsParent();
    } catch (EvalError& e) {
      RecoverFromError(e, op);
      throw EvalError(string("Failed to recover from error: ") + e.what());
    }
  } else {
    // Errors from setupAsParent are recoverable
    try {
      open->setupAsParent();
    } catch (EvalError& e) {
      RecoverFromError(e, open);
      throw EvalError(string("Failed to recover from error: ") + e.what());
    }
  }
  delete n;   // always discard the closing brace/paren
  return parent;    // ascend
}

// Find the nearest enclosing block (ancestor) of the given node, and delete
// the subtree from which it came, and (to be paranoid) any of the block's
// children that follow it (there shouldn't be any).
//
// On success, throws a recoveredError with the new "recoveredPosition" that
// should be the AST's new current position (the cleaned-up block).
// On error, throws its own EvalError.
void Node::RecoverFromError(EvalError& e, Node* problemNode) {
  Node* current = problemNode;
  try {
    while (current && current->parent) {
      Block* parentBlock = dynamic_cast<Block*>(current->parent);
      if (!parentBlock) {
        current = current->parent;
        continue;
      }
      // Find current in the parentBlock's children.  Delete it and any
      // subsequent children.
      parentBlock->removeChildrenStartingAt(current);
      throw RecoveredError(e, parentBlock);
    }
  } catch (EvalError& x) {
    throw EvalError(string("Cannot recover from error '") + e.what() + "': " + x.what());
  }
  if (!current || current->parent || !dynamic_cast<RootNode*>(current)) {
    throw EvalError(string("Cannot recover from error '") + e.what() + "': unknown error");
  }
  // We made it to the root node.
  throw RecoveredError(e, current);
}

/* Members */

Node::Node(Log& log, RootNode*const root, const Token& token)
  : log(log),
    root(root),
    name(token.name),
    value(token.value),
    isInit(false),
    isSetup(false),
    isAnalyzed(false),
    isEvaluated(false),
    parent(NULL),
    parentScope(NULL) {
}

Node::~Node() {
  log.debug("Destroying node " + name);
  for (child_iter i = children.begin(); i != children.end(); ++i) {
    delete *i;
  }
}

// This is a very early scope initialization.  We don't necessarily have a Node
// parent, and even if we do it may not be trustworthy.  Instead, use the
// provided scopeParent to be something we can use as the parent scope (and if
// it does not itself define the scope, then our parent scope is its parent
// scope).  We even allow nodes to (carefully!) override their own initScope()
// in case they have a real scope as a member that needs to be initialized.
void Node::initScopeNode(Node* scopeParent) {
  if (this == root) {
    throw EvalError("Cannot init scope for the root node");
  } else if (!scopeParent) {
    throw EvalError("Cannot init " + print() + " scope with no scope parent");
  }
  parentScope = scopeParent->getScope();
  if (!parentScope) {
    parentScope = scopeParent->getParentScope();
  }
  initScope(scopeParent);
  isInit = true;
}

void Node::replaceChild(Node* oldChild, Node* newChild) {
  string oldPrint = print();
  bool replaced = false;
  for (child_mod_iter i = children.begin(); i != children.end(); ++i) {
    if (*i == oldChild) {
      *i = newChild;
      replaced = true;
      break;
    }
  }
  if (!replaced) {
    throw EvalError("Failed to replace " + oldChild->print() + " with " + newChild->print() + " in " + print());
  }
  log.debug("Replaced " + oldChild->print() + " in " + oldPrint + " with " + newChild->print() + " to become " + print());
}

// Called only on nodes that are understood to be parents.
// We setupNode() the nodes children-first.
void Node::setupAsParent() {
  // Note: the node's grandchildren should all already be setup.
  for (child_iter i = children.begin(); i != children.end(); ++i) {
    (*i)->setupNode();
  }
  Expression* exp = dynamic_cast<Expression*>(this);
  TypeSpec* typespec = dynamic_cast<TypeSpec*>(this);
  if (exp || typespec) {
    // hm, can we rethink what we're doing?  Maybe there's a more general class
    // of things that don't want setup() to happen until they are done, that
    // is, defer setting up the intermediaries.  *ahem* object literal *ahem*.
    // What about function?  Maybe it really is a bad language design decision
    // to not let functions be analyzed properly until the expression they're a
    // part of is analyzed.  Humph...
    //
    // I wonder if we can know the precedence of & well enough that we actually
    // can evaluate ObjLits and Functions as they are typed -- i.e. we can look
    // back for anything that's already been declared (parent types we know)
    // and just not allow YET anything whose parent type has not been, um,
    // typed.
    // PRATT PARSING TO THE RESCUE!! but we will add hax to detect where this
    // might be needed, up in InsertNode()....
  }
  setupNode();
  log.debug("Setup node " + print());
}

void Node::setupNode() {
  if (isSetup) return;
  if (!isInit) {
    throw EvalError("Cannot setup Node " + print() + " until it's init");
  }
  if (!parent) {
    throw EvalError("Cannot setup Node " + print() + " with no parent");
  }
  log.debug(" - setting up node " + print());
  setup();
  isSetup = true;
  log.debug(" - analyzing node " + print());
  analyzeNode();
  isAnalyzed = true;
}

void Node::analyzeNode() {
  if (isAnalyzed) return;
  if (!isInit || !isSetup) {
    throw EvalError("Node " + print() + " cannot do static analysis until init and setup");
  }

  Statement* statement = dynamic_cast<Statement*>(this);
  if (statement) {
    log.debug(" - - analyzing statement " + print());
    statement->analyze();
  }
}

void Node::evaluateNode() {
  if (isEvaluated) {
    throw EvalError("Node " + print() + " has already been evaluated");
  }
  if (!isInit || !isSetup || !isAnalyzed) {
    // An immediate child of the root can skip evaluation if it's not setup
    if (root == parent && root != NULL) return;
    throw EvalError("Node " + print() + " cannot be evaluated until init, setup, and analyzed");
  }
  // Evaluate nodes children-first
  for (child_iter i = children.begin(); i != children.end(); ++i) {   
    (*i)->evaluateNode();
  }
  log.debug(" - evaluating node " + print());
  evaluate();
  isEvaluated = true;
}

string Node::print() const {
  string r = name;
  if (value.length() > 0) {
    r += ":" + value;
  }
  if (children.size() > 0) {
    r += "(";
    for (child_iter i = children.begin(); i != children.end(); ++i) {
      if (i != children.begin()) r += " ";
      r += (*i)->print();
    }
    r += ")";
  }
  return r;
}

Node::operator std::string() const {
  return name;
}

/* protected */

void Node::addChild(Node* child) {
  children.push_back(child);
}

void Node::removeChildrenStartingAt(const Node* child) {
  log.debug("Removing children from " + print() + " starting at " + child->print());
  int foundChildren = 0;
  for (child_iter i = children.begin(); i != children.end(); ++i) {
    if (child == *i || foundChildren > 0) {
      ++foundChildren;
      delete *i;
      child = NULL;
    }
  }
  for (int i=0; i < foundChildren; ++i) {
    children.pop_back();
  }
}
