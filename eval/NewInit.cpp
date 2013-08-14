// Copyright (C) 2013 Michael Biggs.  See the LICENSE file at the top-level
// directory of this distribution and at http://lush-shell.org/copyright.html

#include "NewInit.h"

#include "Block.h"
#include "EvalError.h"
#include "Type.h"
#include "Variable.h"

#include <string>
using std::string;

using namespace eval;

NewInit::~NewInit() {
  // Revert our object if we've partially created it.
  // Be paranoid here since this is regarding error conditions.
  if (m_isPrepared && parentScope && m_varname != "" &&
      parentScope->getObject(m_varname)) {
    parentScope->revert(m_varname);
  }
}

void NewInit::setup() {
  if (!parentScope) {
    throw EvalError("Cannot setup NewInit " + print() + " with no parent scope");
  }
  if (children.size() < 1 || children.size() > 3) {
    throw EvalError("NewInit node must have 1, 2, or 3 children");
  }
  if (m_variable || m_exp || m_typeSpec || m_type.get()) {
    throw EvalError("NewInit node " + print() + " is already partially setup");
  }
  m_variable = dynamic_cast<Variable*>(children.at(0));
  if (!m_variable) throw EvalError("NewInit's first child must be a variable");
  m_varname = m_variable->getVariableName();
  log.info("NewInit varname is " + m_varname);
  switch (children.size()) {
    // new x -- type and value are both 'object'
    case 1: {
      // TODO get this directly from the global scope
      Object* object = parentScope->getObject("object");
      if (!object) {
        throw EvalError("Cannot find the object object.  Uhoh.");
      }
      m_type.reset(new BasicType(*object));
      // leave m_typeSpec NULL
      // leave m_exp NULL
      break;
    }

    // new x : y -- initial value is the result of the Expression 'y', and our
    // type is its type
    case 2: {
      m_exp = dynamic_cast<Expression*>(children.at(1));
      if (!m_exp) {
        throw EvalError("NewInit child " + children.at(1)->print() + " should have been an Expression");
      }
      m_type = m_exp->getType();
      // leave m_typeSpec NULL
      break;
    }

    // new x : y = z -- type is 'y', initial value is 'z'
    case 3: {
      m_typeSpec = dynamic_cast<TypeSpec*>(children.at(1));
      if (!m_typeSpec) {
        throw EvalError("NewInit child " + children.at(1)->print() + " should have been a TypeSpec");
      }
      m_exp = dynamic_cast<Expression*>(children.at(2));
      if (!m_exp) {
        throw EvalError("NewInit child " + children.at(2)->print() + " should have been an Expression");
      }
      m_type = m_typeSpec->getType();

      // TODO: Validate that the initial value matches the type
      /*
      if (!m_type->isCompatible(m_exp->getType())) {
        throw EvalError("Value does not match the type of variable " + m_varname);
      }
      */

      break;
    }
    default:
      throw EvalError("NewInit node must have 1, 2, or 3 children");
  }
}

void NewInit::prepare() {
  if (!parentScope) {
    throw EvalError("Cannot prepare NewInit " + print() + " with no parent scope");
  } else if (parentScope->getObject(m_varname)) {
    throw EvalError("Variable " + m_varname + " already exists");
  } else if (!m_type.get()) {
    throw EvalError("Cannot prepare NewInit " + print() + " which has not determined the new object's Type");
  }

  // Construct the object.  It takes ownership of its type, which we need no
  // longer.
  m_object = new Object(log, m_varname, *m_type.get());

  // Insert into scope.  Scope takes ownership of the Object; but we keep the
  // Object* so that we can assign its initial value during evaluate().
  parentScope->newObject(m_varname, m_object);
  m_isPrepared = true;
}

// Commit the new object to our enclosing scope, and assign its initial value
void NewInit::evaluate() {
  // TODO: remove this when we are confident it can't happen
  if (!m_isPrepared) {
    throw EvalError("Cannot evaluate NewInit node until it has been prepared");
  }
  parentScope->commit(m_varname);
  m_isPrepared = false;
  // TODO assign initial value
  //m_object->assign(m_exp);
}