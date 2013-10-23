/*
 * Copyright 2013, Mirek Klimos <myreggg@gmail.com>
 */
#include <cstdio>
#include <algorithm>
#include <string>

#include "./formula.h"
#include "./util.h"

void Construct::dump(int indent) {
  for (int i = 0; i < indent; ++i) {
    printf("   ");
  }
  printf("%p: %s\n", (void*)this, name().c_str());
  for (uint i = 0; i < child_count(); ++i) {
    child(i)->dump(indent + 1);
  }
}

int Variable::id_counter_ = 0;

Construct* AndOperator::Simplify() {
  Construct::Simplify();
  int merged = 0;
  int childCount = child_count();
  for (int i = 0; i < childCount; ++i) {
    auto andChild = dynamic_cast<AndOperator*>(child(i - merged));
    if (andChild) {
      removeChild(i - merged);
      addChildren(andChild->children_);
      merged++;
    }
  }
  return this;
}

Construct* OrOperator::Simplify() {
  Construct::Simplify();
  int merged = 0;
  int childCount = child_count();
  for (int i = 0; i < childCount; ++i) {
    auto orChild = dynamic_cast<OrOperator*>(child(i - merged));
    if (orChild) {
      removeChild(i - merged);
      addChildren(orChild->children_);
      merged++;
    }
  }
  return this;
}

// Tseitin transformation of arbitrary formula to CNF
AndOperator* Formula::ToCnf() {
  FormulaVec clauses;
  // recursively create clauses capturing relationships between nodes
  TseitinTransformation(clauses);
  // the variable corresponding to the root node must be always true
  clauses.push_back(m.get<OrOperator>(FormulaVec{ tseitin_var() }));
  auto root = m.get<AndOperator>(clauses);
  return root;
}

// return the variable corresponding to the node during Tseitin transformation
Formula* Formula::tseitin_var() {
  if (isSimple()) return this;
  if (!tseitin_var_) tseitin_var_ = m.get<Variable>();
  return tseitin_var_;
}

AndOperator* AtLeastOperator::Expand() {
  FormulaVec vars;
  std::function<Formula*(Formula*)> tseitin_vars = [](Formula* f){ return f->tseitin_var(); };
  transform(children_, vars, tseitin_vars);
  auto root = m.get<AndOperator>();
  std::function<void(FormulaVec)> addToNode =
    [&](FormulaVec l) {
    root->addChild(m.get<OrOperator>(l));
  };
  for_all_combinations(vars.size() - value_ + 1, vars, addToNode);
  return root;
}

AndOperator* AtMostOperator::Expand() {
  FormulaVec vars;
  std::function<Formula*(Formula*)> tseitin_vars = [](Formula* f){ return f->tseitin_var(); };
  transform(children_, vars, tseitin_vars);
  auto root = m.get<AndOperator>();
  std::function<void(FormulaVec)> addToNode =
    [&](FormulaVec l) {
    FormulaVec negl;
    negl.resize(l.size());
    std::transform(l.begin(), l.end(), negl.begin(), [](Formula* f){ return m.get<NotOperator>(f); });
    root->addChild(m.get<OrOperator>(negl));
  };
  for_all_combinations(value_ + 1, vars, addToNode);
  return root;
}

AndOperator* ExactlyOperator::Expand() {
  FormulaVec vars;
  std::function<Formula*(Formula*)> tseitin_vars = [](Formula* f){ return f->tseitin_var(); };
  transform(children_, vars, tseitin_vars);
  auto root = m.get<AndOperator>();
  // At most part
  std::function<void(FormulaVec)> addToNode =
    [&](FormulaVec l) {
    FormulaVec negl;
    negl.resize(l.size());
    std::transform(l.begin(), l.end(), negl.begin(), [](Formula* f){ return m.get<NotOperator>(f); });
    root->addChild(m.get<OrOperator>(negl));
  };
  for_all_combinations(value_ + 1, vars, addToNode);
  // At least part
  addToNode =
    [&](FormulaVec l){
    root->addChild(m.get<OrOperator>(l));
  };
  for_all_combinations(vars.size() - value_ + 1, vars, addToNode);
  return root;
}

void AndOperator::TseitinTransformation(FormulaVec& clauses) {
  // X <-> AND(A1, A2, ..)
  // (X | !A1 | !A2 | ...) & (A1 | !X) & (A2 | !X) & ...
  FormulaVec first;
  first.resize(children_.size());
  std::transform(children_.begin(), children_.end(), first.begin(), [](Formula* f) {
    return m.get<NotOperator>(f->tseitin_var());
  });
  first.push_back(tseitin_var());
  clauses.push_back(m.get<OrOperator>(first));

  auto not_this = m.get<NotOperator>(tseitin_var());
  for (auto& f: children_) {
    clauses.push_back(m.get<OrOperator>(FormulaVec{
      not_this, f->tseitin_var()
    }));
  }

  // recurse down
  for (auto&f : children_) {
    f->TseitinTransformation(clauses);
  }
}

void OrOperator::TseitinTransformation(FormulaVec& clauses) {
  // X <-> OR(A1, A2, ..)
  // (!X | A1 | A2 | ...) & (!A1 | X) & (!A2 | X) & ...
  FormulaVec first;
  first.resize(children_.size());
  std::transform(children_.begin(), children_.end(), first.begin(), [](Formula* f) {
    return f->tseitin_var();
  });
  first.push_back(m.get<NotOperator>(tseitin_var()));
  clauses.push_back(m.get<OrOperator>(first));

  for (auto& f: children_) {
    clauses.push_back(m.get<OrOperator>(FormulaVec{
      tseitin_var(), m.get<NotOperator>(f->tseitin_var())
    }));
  }

  // recurse down
  for (auto&f : children_) {
    f->TseitinTransformation(clauses);
  }
}

void NotOperator::TseitinTransformation(FormulaVec& clauses) {
  // X <-> (!Y)
  // (!X | !Y) & (X | Y)
  auto thisVar = tseitin_var();
  auto childVar = child_->tseitin_var();
  auto notThis = m.get<NotOperator>(thisVar);
  auto notChild = m.get<NotOperator>(childVar);
  clauses.push_back(m.get<OrOperator>(FormulaVec{notThis, notChild}));
  clauses.push_back(m.get<OrOperator>(FormulaVec{thisVar, childVar}));
  child_->TseitinTransformation(clauses);
}

void ImpliesOperator::TseitinTransformation(FormulaVec& clauses) {
  // X <-> (L -> R)
  // (!X | !L | R) & (L | X) & (!R | X)
  auto thisVar = tseitin_var();
  auto leftVar = left_->tseitin_var();
  auto rightVar = right_->tseitin_var();
  auto notThis = m.get<NotOperator>(thisVar);
  auto notLeft = m.get<NotOperator>(leftVar);
  auto notRight = m.get<NotOperator>(rightVar);
  clauses.push_back(m.get<OrOperator>(FormulaVec{notThis, notLeft, rightVar}));
  clauses.push_back(m.get<OrOperator>(FormulaVec{leftVar, thisVar}));
  clauses.push_back(m.get<OrOperator>(FormulaVec{notRight, thisVar}));

  left_->TseitinTransformation(clauses);
  right_->TseitinTransformation(clauses);
}

void EquivalenceOperator::TseitinTransformation(FormulaVec& clauses) {
  // X <-> (L <-> R)
  // (X | L | R) & (!X | L | R) & (X | !L | R) & (X | L | !R)
  auto thisVar = tseitin_var();
  auto leftVar = left_->tseitin_var();
  auto rightVar = right_->tseitin_var();
  auto notThis = m.get<NotOperator>(thisVar);
  auto notLeft = m.get<NotOperator>(leftVar);
  auto notRight = m.get<NotOperator>(rightVar);
  clauses.push_back(m.get<OrOperator>(FormulaVec{thisVar, leftVar, rightVar}));
  clauses.push_back(m.get<OrOperator>(FormulaVec{notThis, leftVar, rightVar}));
  clauses.push_back(m.get<OrOperator>(FormulaVec{thisVar, notLeft, rightVar}));
  clauses.push_back(m.get<OrOperator>(FormulaVec{thisVar, leftVar, notRight}));

  left_->TseitinTransformation(clauses);
  right_->TseitinTransformation(clauses);
}

void AtMostOperator::TseitinTransformation(FormulaVec& clauses) {
  auto expanded = Expand();
  expanded->addChild(m.get<EquivalenceOperator>(tseitin_var(), expanded->tseitin_var()));
  expanded->TseitinTransformation(clauses);
}

void AtLeastOperator::TseitinTransformation(FormulaVec& clauses) {
  auto expanded = Expand();
  expanded->addChild(m.get<EquivalenceOperator>(tseitin_var(), expanded->tseitin_var()));
  expanded->TseitinTransformation(clauses);
}

void ExactlyOperator::TseitinTransformation(FormulaVec& clauses) {
  auto expanded = Expand();
  expanded->addChild(m.get<EquivalenceOperator>(tseitin_var(), expanded->tseitin_var()));
  expanded->TseitinTransformation(clauses);
}
