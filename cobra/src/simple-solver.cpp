/*
 * Copyright 2014, Mirek Klimos <myreggg@gmail.com>
 */

#include "formula.h"
#include "simple-solver.h"
#include "pico-solver.h"

SolverStats SimpleSolver::stats_ = SolverStats();

SimpleSolver::SimpleSolver(const vec<Variable*>& vars,
                           Formula* restriction) :
    vars_(vars),
    restriction_(restriction) {
  PicoSolver sat(vars, restriction);
  codes_ = sat.GenerateModels();
  for (uint i = 0; i < codes_.size(); i++) {
    sat_.push_back(i);
  }
  ready_ = true;
}

void SimpleSolver::AddConstraint(Formula* formula) {
  ready_ = false;
  constraints_.push_back(make_pair(formula, vec<CharId>()));
}

void SimpleSolver::AddConstraint(Formula* formula, const vec<CharId>& params) {
  ready_ = false;
  constraints_.push_back(make_pair(formula, params));
}

void SimpleSolver::OpenContext() {
  contexts_.push_back(constraints_.size());
  context_unsat_.push_back(vec<uint>());
}

void SimpleSolver::CloseContext() {
  assert(contexts_.size() > 0);
  // remove constraints added in this context
  auto k = contexts_.back();
  constraints_.erase(constraints_.begin() + k, constraints_.end());

  // add codes removed from sat_ in this context
  auto& removed = context_unsat_.back();
  sat_.insert(sat_.begin(), removed.begin(), removed.end());

  contexts_.pop_back();
  context_unsat_.pop_back();
  ready_ = false;
}

bool SimpleSolver::_MustBeTrue(VarId id) {
  if (!ready_) Update();
  for (auto& x: sat_)
    if (!codes_[x][id]) return false;
  return true;
}

bool SimpleSolver::_MustBeFalse(VarId id) {
  if (!ready_) Update();
  for (auto& x: sat_)
    if (codes_[x][id]) return false;
  return true;
}

uint SimpleSolver::_GetNumOfFixedVars() {
  if (!ready_) Update();
  vec<bool> canbe[2];
  canbe[0].resize(vars_.size());
  canbe[1].resize(vars_.size());
  for (auto x: sat_)
    for (uint i = 1; i < vars_.size(); i++)
      canbe[codes_[x][i]][i] = true;
  uint f = 0;
  for (uint i = 1; i < vars_.size(); i++)
    if (!(canbe[0][i] && canbe[1][i])) f++;
  return f;
}

bool SimpleSolver::_Satisfiable() {
  if (!ready_) Update();
  return !sat_.empty();
}

bool SimpleSolver::_OnlyOneModel() {
  assert(ready_);
  assert(!sat_.empty());
  return sat_.size() == 1;
}

vec<bool> SimpleSolver::GetAssignment() {
  assert(!sat_.empty());
  return codes_[sat_[0]];
}

void SimpleSolver::PrintAssignment() {
  assert(!sat_.empty());
  auto& a = codes_[sat_[0]];
  vec<int> trueVar;
  vec<int> falseVar;
  for (uint id = 1; id < vars_.size(); id++) {
    if (a[id]) trueVar.push_back(id);
    else falseVar.push_back(id);
  }
  printf("TRUE: ");
  for (auto s: trueVar) printf("%s ", vars_[s]->ident().c_str());
  printf("\nFALSE: ");
  for (auto s: falseVar) printf("%s ", vars_[s]->ident().c_str());
  printf("\n");
}

uint SimpleSolver::_NumOfModels() {
  if (!ready_) Update();
  return sat_.size();
}

vec<vec<bool>> SimpleSolver::_GenerateModels() {
  if (!ready_) Update();
  vec<vec<bool>> result;
  for (auto x: sat_)
    result.push_back(codes_[x]);
  return result;
}

void SimpleSolver::Update() {
  vec<uint> sat_now;
  for (auto i: sat_) {
    bool ok = true;
    for (auto& constr: constraints_) {
      if (!constr.first->Satisfied(codes_[i], constr.second)) {
        ok = false;
        break;
      }
    }
    if (ok) {
      sat_now.push_back(i);
    } else if (!context_unsat_.empty()) {
      auto& removed = context_unsat_.back();
      removed.push_back(i);
    }
  }
  sat_.swap(sat_now);
  ready_ = true;
}

string SimpleSolver::pretty() {
  string s = restriction_->pretty(false) + " & ";
  for (auto& c: constraints_) {
    s += c.first->pretty(false, &c.second) + " & ";
  }
  s.erase(s.length()-3, 3);
  return s;
}