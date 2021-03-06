// Copyright (C) 2013 Michael Biggs.  See the COPYING file at the top-level
// directory of this distribution and at http://shok.io/code/copyright.html

#ifndef _Block_h_
#define _Block_h_

/* Block construct
 *
 * The Token (construction-time) does not have enough information to know if
 * this is a code block (list of statements) or an expression block (single
 * expression).  We have to wait until setup()-time to determine which.
 */

#include "Brace.h"
#include "Expression.h"
#include "Log.h"
#include "RootNode.h"
#include "Token.h"
//#include "Statement.h"
#include "Variable.h"

#include <map>

namespace eval {

class Block : public Brace {
public:
  Block(Log& log, RootNode*const root, const Token& token)
    : Brace(log, root, token, true),
      m_scope(log),
      m_exp(NULL) {}
  ~Block();

  virtual void initScope(Node* scopeParent);
  virtual void setup();
  virtual void evaluate();
  virtual std::string cmdText() const;

  bool isCodeBlock() const { return !m_exp; }
  virtual Scope* getScope() { return &m_scope; }

private:
  Expression* m_exp;
  Scope m_scope;
};

};

#endif // _Block_h_
