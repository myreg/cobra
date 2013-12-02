/*
 * Copyright 2013, Mirek Klimos <myreggg@gmail.com>
 */

#include <vector>
#include <map>
#include <set>

extern "C" {
  #include "sat-solvers/picosat/picosat.h"
}

#ifndef COBRA_CNF_FORMULA_H_
#define COBRA_CNF_FORMULA_H_

class Variable;
class Formula;
class FormulaList;

typedef std::set<int> TClause;

class CnfFormula {
  std::set<TClause> clauses_;
  std::map<int, Variable*> variables_;
  std::set<int> original_;
  PicoSAT* picosat_;

 private:
  Variable* getVariable(int id);
  void addVariable(Variable* var);
  void addLiteral(TClause& clause, Formula* literal);

  std::string pretty_literal(int id, bool unicode = true);
  std::string pretty_clause(const TClause& clause, bool unicode = true);

  bool ProbeEquivalence(const TClause& clause, int var1, int var2);

 public:
  CnfFormula() {
    picosat_ = picosat_init();
  }

  void addClause(FormulaList* list);
  void addClause(std::initializer_list<Formula*> list);
  void AddConstraint(Formula* formula);
  void AddConstraint(CnfFormula& cnf);

  int GetFixedVariables();
  bool Satisfiable();
  void PrintAssignment();

  std::map<int, int> ComputeVariableEquivalence();

  std::string pretty(bool unicode = true);
};

#endif   // COBRA_CNF_FORMULA_H_
