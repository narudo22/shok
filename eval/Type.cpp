// Copyright (C) 2013 Michael Biggs.  See the COPYING file at the top-level
// directory of this distribution and at http://shok.io/code/copyright.html

#include "Type.h"

#include <algorithm>
#include <string>
using std::auto_ptr;
using std::string;

using namespace eval;

/* NullType */

Object* NullType::getMember(const string& name) const {
  return NULL;
}

auto_ptr<Type> NullType::getMemberType(const string& name) const {
  return auto_ptr<Type>(NULL);
}

bool NullType::isCompatible(const Type& rhs) const {
  throw EvalError("NullType cannot be checked for compatibility");
}

/*
TypeScore NullType::compatibilityScore(const Type& rhs, TypeScore score) const {
  return TypeScore(false);
}
*/

auto_ptr<Type> NullType::duplicate() const {
  throw EvalError("Cannot duplicate the root type");
}

string NullType::print() const {
  return "<no type>";
}

/* BasicType */

Object* BasicType::getMember(const string& name) const {
  return m_object.getMember(name);
}

auto_ptr<Type> BasicType::getMemberType(const string& name) const {
  Object* member = m_object.getMember(name);
  if (!member) {
    return auto_ptr<Type>(NULL);
  }
  return member->getType().duplicate();
}

bool BasicType::isCompatible(const Type& rhs) const {
  // For compatibility, rhs could be either:
  //  - a BasicType of a descendent of our Object
  //  - an AndType where one of the children is compatible
  //  - an OrType where both children are compatible
  // In the AndType case, no more than one of the children should be
  // compatible, but this should have been enforced by the AndType's
  // construction, so we don't need to check it here.
  const BasicType* basicType = dynamic_cast<const BasicType*>(&rhs);
  const AndType* andType = dynamic_cast<const AndType*>(&rhs);
  const OrType* orType = dynamic_cast<const OrType*>(&rhs);
  if (basicType) {
    if (&basicType->m_object == &m_object) return true;
    if (basicType->m_object.getType().isNull()) return false;
    return basicType->m_object.getType().isCompatible(rhs);
  } else if (andType) {
    return andType->left().isCompatible(rhs) ||
           andType->right().isCompatible(rhs);
  } else if (orType) {
    return orType->left().isCompatible(rhs) &&
           orType->right().isCompatible(rhs);
  }
  throw EvalError("Cannot check compatibility for unknown Type " + rhs.print());
}

/*
TypeScore BasicType::compatibilityScore(const Type& rhs,
                                        TypeScore score) const {
  // For compatibility, rhs must be either a BasicType of a descendent of our
  // Object, or an AndType where one of the children is compatible.  In the
  // AndType case, no more than one of the children should be compatible, but
  // this should have been enforced by the AndType's construction, so we don't
  // need to check it here.
  const BasicType* basicType = dynamic_cast<BasicType*>(&rhs);
  const AndType* andType = dynamic_cast<AndType*>(&rhs);
  if (basicType) {
    if (&basicType->m_object == &m_object) return score;
    return compatibilityScore(basicType->m_object.getType(), score+1);
  } else if (andType) {
    return std::min(compatibilityScore(andType->left(), score+1),
                    compatibilityScore(andType->right(), score+1));
  }
  return TypeScore(false);
}
*/

auto_ptr<Type> BasicType::duplicate() const {
  return auto_ptr<Type>(new BasicType(m_object));
}

string BasicType::print() const {
  return m_object.print();
}

/* AndType */

Object* AndType::getMember(const string& name) const {
  // It should only exist in one of the children, but that should have been
  // enforced at construction.
  Object* o = m_left->getMember(name);
  if (o) return o;
  return m_right->getMember(name);
}

auto_ptr<Type> AndType::getMemberType(const string& name) const {
  if (!m_left.get() || !m_right.get()) {
    throw EvalError("Cannot get member type of deficient AndType " + print());
  }
  auto_ptr<Type> type(m_left->getMemberType(name));
  if (type.get()) return type;
  return m_right->getMemberType(name);
}

bool AndType::isCompatible(const Type& rhs) const {
  const BasicType* basicType = dynamic_cast<const BasicType*>(&rhs);
  const AndType* andType = dynamic_cast<const AndType*>(&rhs);
  const OrType* orType = dynamic_cast<const OrType*>(&rhs);
  if (basicType) {
    // A&B <=> C ?  (C matches A) or (C matches B)
    return m_left->isCompatible(*basicType) ||
           m_right->isCompatible(*basicType);
  } else if (andType) {
    // A&B <=> C&D ?  (C or D matches A) or (C or D matches B)
    return m_left->isCompatible(andType->left()) ||
           m_right->isCompatible(andType->right()) ||
           m_left->isCompatible(andType->left()) ||
           m_right->isCompatible(andType->right());
  } else if (orType) {
    // A&B <=> C|D ?  (C matches A or B) and (D matches A or B)
    return (m_left->isCompatible(andType->left()) ||
            m_right->isCompatible(andType->left())) &&
           (m_left->isCompatible(andType->right()) ||
            m_right->isCompatible(andType->right()));
  }
  throw EvalError("Cannot check compatibility for unknown Type " + rhs.print());
}

/*
TypeScore AndType::compatibilityScore(const Type& rhs, TypeScore score) const {
  const BasicType* basicType = dynamic_cast<BasicType*>(&rhs);
  const AndType* andType = dynamic_cast<AndType*>(&rhs);
  const OrType* orType = dynamic_cast<OrType*>(&rhs);
  if (basicType) {
    // A&B <=> C ?  (C score at matching A) + (C score at matching B)
    return m_left.compatibilityScore(basicType) +
           m_right.compatibilityScore(basicType);
  } else if (andType) {
    // A&B <=> C&D ?  (which of C&D matches A) + (which of C&D matches B)
    return std::min(m_left.compatibilityScore(andType.left()),
                    m_left.compatibilityScore(andType.right())) +
           std::min(m_right.compatibilityScore(andType.left()),
                    m_right.compatibilityScore(andType.right()));
  } else if (orType) {
    // A&B <=> C|D ?  min(C score at matching A&B, D score at matching A&B)
    return std::min(compatibilityScore(orType.left()),
                    compatibilityScore(orType.right()));
  }
  return TypeScore(false);
}
*/

auto_ptr<Type> AndType::duplicate() const {
  if (!m_left.get() || !m_right.get()) {
    throw EvalError("Cannot duplicate deficient AndType " + print());
  }
  return auto_ptr<Type>(new AndType(*m_left.get(), *m_right.get()));
}

string AndType::print() const {
  return "&(" + m_left->print() + "," + m_right->print() + ")";
}

/* OrType */

auto_ptr<Type> OrType::OrUnion(const Type& a, const Type& b) {
  if (a.isCompatible(b)) return a.duplicate();
  if (b.isCompatible(a)) return b.duplicate();
  return auto_ptr<Type>(new OrType(a, b));
}

Object* OrType::getMember(const string& name) const {
  // I don't think this is allowed.
  throw EvalError("Cannot request member " + name + " from OrType " + print());
}

auto_ptr<Type> OrType::getMemberType(const string& name) const {
  // It must exist in both children.  We return the best |-union of the member
  // types.
  if (!m_left.get() || !m_right.get()) {
    throw EvalError("Cannot get member type of deficient OrType " + print());
  }
  auto_ptr<Type> leftType(m_left->getMemberType(name));
  auto_ptr<Type> rightType(m_right->getMemberType(name));
  if (!leftType.get() || !rightType.get()) {
    throw EvalError("A child of OrType " + print() + " has a deficient Type.");
  }
  return OrUnion(*leftType.get(), *rightType.get());
}

bool OrType::isCompatible(const Type& rhs) const {
  const BasicType* basicType = dynamic_cast<const BasicType*>(&rhs);
  const AndType* andType = dynamic_cast<const AndType*>(&rhs);
  const OrType* orType = dynamic_cast<const OrType*>(&rhs);
  if (basicType) {
    // A|B <=> C ?  (C matches A) or (C matches B)
    return m_left->isCompatible(*basicType) ||
           m_right->isCompatible(*basicType);
  } else if (andType) {
    // A|B <=> C&D ?  (C or D match A) or (C or D match B)
    return m_left->isCompatible(andType->left()) ||
           m_left->isCompatible(andType->right()) ||
           m_right->isCompatible(andType->left()) ||
           m_right->isCompatible(andType->right());
  } else if (orType) {
    // A|B <=> C|D ?  (C and D match A) or (C and D match B)
    return (m_left->isCompatible(orType->left()) &&
            m_left->isCompatible(orType->right())) ||
           (m_right->isCompatible(orType->left()) &&
            m_right->isCompatible(orType->right()));
  }
  throw EvalError("Cannot check compatibility for unknown Type " + rhs.print());
}

/*
TypeScore OrType::compatibilityScore(const Type& rhs, TypeScore score) const {
  const BasicType* basicType = dynamic_cast<BasicType*>(&rhs);
  const AndType* andType = dynamic_cast<AndType*>(&rhs);
  const OrType* orType = dynamic_cast<OrType*>(&rhs);
  if (basicType) {
    // A|B <=> C ?  min(C score at matching A, C score at matching B)
    return std::min(m_left.compatibilityScore(basicType),
                    m_right.compatibilityScore(basicType));
  } else if (andType) {
    // A|B <=> C&D ?  min(C&D score at matching A, C&D score at matching B)
    return std::min(m_left.compatibilityScore(andType),
                    m_right.compatibilityScore(andType));
  } else if (orType) {
    // A|B <=> C|D ?  Oh dear, it's really unclear if we should be taking the
    // min of possibilities or the max of some.  How can we possibly determine
    // which function to call if we haven't really figured out what the
    // caller's argument's type (object) really is?
  }
  return TYPE_INCOMPATIBLE;
}
*/

auto_ptr<Type> OrType::duplicate() const {
  if (!m_left.get() || !m_right.get()) {
    throw EvalError("Cannot duplicate deficient OrType " + print());
  }
  return auto_ptr<Type>(new OrType(*m_left.get(), *m_right.get()));
}

string OrType::print() const {
  if (!m_left.get() || !m_right.get()) return "|(uninitialized)";
  return "|(" + m_left->print() + "," + m_right->print() + ")";
}
