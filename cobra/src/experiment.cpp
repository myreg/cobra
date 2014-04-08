/*
 * Copyright 2014, Mirek Klimos <myreggg@gmail.com>
 */

#include <vector>
#include <map>
#include <exception>
#include <cassert>
#include <bliss/graph.hh>
#include <bliss/utils.hh>

#include "common.h"
#include "formula.h"
#include "game.h"
#include "parser.h"
#include "experiment.h"

extern Parser m;

void Option::ComputeSat() {
  sat_.resize(type_.outcomes().size());
  sat_num_ = 0;
  for (uint i = 0; i < type_.outcomes().size(); i++) {
    cnf_.OpenContext();
    cnf_.AddConstraint(type_.outcomes()[i], params_);
    sat_[i] = cnf_.Satisfiable();
    sat_num_ += sat_[i];
    cnf_.CloseContext();
  }
}

void Option::ComputeNumOfModels() {
  models_.resize(type_.outcomes().size());
  models_total_ = 0;
  for (uint i = 0; i < type_.outcomes().size(); i++) {
    cnf_.OpenContext();
    cnf_.AddConstraint(type_.outcomes()[i], params_);
    models_[i] = cnf_.NumOfModels();
    models_total_ += models_[i];
    cnf_.CloseContext();
  }
}

void Option::ComputeFixedVars() {
  fixed_.resize(type_.outcomes().size());
  for (uint i = 0; i < type_.outcomes().size(); i++) {
    cnf_.OpenContext();
    cnf_.AddConstraint(type_.outcomes()[i], params_);
    fixed_[i] = cnf_.GetNumOfFixedVars();
    cnf_.CloseContext();
  }
}

void Experiment::addOutcome(string name, Formula* outcome) {
  outcomes_names_.push_back(name);
  outcomes_.push_back(outcome);
}

void Experiment::paramsDistinct(vec<uint>* list) {
  for(auto i = list->begin(); i != list->end(); ++i) {
    for(auto j = i + 1; j != list->end(); ++j) {
      m.input_assert(*i > 0 && *i <= num_params_,
        "Invalid parameter id in PARAMS_DISTINCT.");
      m.input_assert(*j > 0 && *j <= num_params_,
        "Invalid parameter id in PARAMS_DISTINCT.");
      // params are internally indexed from 0, that's why *i - 1
      params_different_[*i - 1].insert(*j - 1);
      params_different_[*j - 1].insert(*i - 1);
    }
  }
  delete list;
}

void Experiment::paramsSorted(vec<uint>* list) {
  for(auto i = list->begin(); i != list->end(); ++i) {
    for(auto j = i + 1; j != list->end(); ++j) {
      m.input_assert(*i > 0 && *i < *j && *j <= num_params_,
        "Invalid parameter id or invalid order in PARAMS_SORTED.");
      // params are internally indexed from 0, that's why *i - 1
      params_smaller_than_[*j - 1].insert(*i - 1);
    }
  }
  delete list;
}

void Experiment::PrecomputeUsed(Formula* f) {
  assert(f);
  auto* mapping = dynamic_cast<Mapping*>(f);
  auto* variable = dynamic_cast<Variable*>(f);
  if (mapping) {
    //printf("%i %i\n", mapping->param_id(), num_params_);
    assert(mapping->param_id() < num_params_);
    used_maps_[mapping->param_id()].insert(mapping->mapping_id());
  } else if (variable) {
    used_vars_.insert(variable->id());
  } else {
    for (uint i = 0; i < f->child_count(); i++)
      PrecomputeUsed(f->child(i));
  }
}

void Experiment::Precompute() {
  // used_maps_, used_vars_
  used_maps_.resize(num_params_);
  for (auto out: outcomes_) {
    PrecomputeUsed(out);
  }

  // interchangable
  interchangable_.resize(num_params_);
  for (uint d = 0; d < num_params_; d++) {
    interchangable_[d].resize(alph_);
    for (CharId a = 0; a < alph_; a++) {
      interchangable_[d][a] = true;
      set<int> vars;
      for (auto& f: used_maps_[d]) {
        auto var = game_->getMappingValue(f, a);
        vars.insert(var);
        if (used_vars_.count(var) > 0) interchangable_[d][a] = false;
      }
      for (uint e = 0; e < num_params_; e++) {
        if (e == d) continue;
        for (CharId b = 0; b < alph_; b++) {
          // check that _b_ can be at _e_, given that _a_ is at _d_
          if (a == b && params_different_[e].count(d) > 0) continue;
          if (a >= b && params_smaller_than_[e].count(d) > 0) continue;
          for (auto f: used_maps_[e]) {
            if (vars.count(game_->getMappingValue(f, b)) > 0)
              interchangable_[d][a] = false;
          }
        }
      }
      //printf("%s %i %i -> %s\n", name_.c_str(), d, a, interchangable_[d][a] ? "true" : "false");
    }
  }
}

bool Experiment::CharsEquiv(set<MapId>& maps, CharId a, CharId b) const {
  bool equiv = true;
  for (auto f: maps) {
    if (gen_var_groups_[game_->getMappingValue(f, a)] !=
        gen_var_groups_[game_->getMappingValue(f, b)]) {
      equiv = false;
    }
  }
  return equiv;
}

set<vec<CharId>>*
Experiment::GenParams(vec<uint>& groups) {
  gen_stats_ = GenParamsStats();
  gen_var_groups_ = groups;
  gen_params_.resize(num_params_);
  gen_params_basic_.clear();
  gen_params_final_.clear();
  GenParamsFill(0);
  //printf("=== Gen params stats: %i %i %i.\n", gen_stats_.ph1, gen_stats_.ph2, gen_stats_.ph3);
  assert(gen_stats_.ph2 == gen_params_basic_.size());
  assert(gen_stats_.ph3 == gen_params_final_.size());
  return &gen_params_final_;
}

// Recursive function that substitudes char at position n for all posibilities.
void Experiment::GenParamsFill(uint n) {
  set<CharId> done;
  for (CharId a = 0; a < alph_; a++) {
    bool valid = true;
    // Test compliance with PARAMS_DIFFERENT.
    for (auto p: params_different_[n]) {
      if (p < n && gen_params_[p] == a) valid = false;
    }
    if (!valid) continue;
    done.insert(a);
    // Test compliance with PARAMS_SORTED.
    for (auto p: params_smaller_than_[n]) {
      assert(p < n);
      if (gen_params_[p] > a) valid = false;
    }
    if (!valid) continue;
    // Test equivalence.
    if (interchangable_[n][a]) {
      for (auto b: done) {
        if (a == b) continue;
        if (CharsEquiv(used_maps_[n], a, b)) {
          valid = false;
          done.erase(a);  // there is an equivalent in done -> not needed
          break;
        }
      }
    }
    if (!valid) continue;
    // Recurse down.
    gen_params_[n] = a;
    if (n + 1 == num_params_) {
      gen_stats_.ph1++;
      GenParamsBasicFilter();
    } else {
      GenParamsFill(n + 1);
    }
  }
}

void Experiment::GenParamsBasicFilter() {
  auto params = gen_params_;
  // Compute position partitioning according to the vars.
  vec<uint> dfu(num_params_, 0);
  for (uint i = 0; i < num_params_; i++) dfu[i] = i;
  for (uint i = 0; i < num_params_; i++)
    for (uint j = i + 1; j < num_params_; j++)
      for (auto fi: used_maps_[i])
        for (auto fj: used_maps_[j])
          if (game_->getMappingValue(fi, params[i]) ==
              game_->getMappingValue(fj, params[j])) {
            if (dfu[i] > dfu[j])
              dfu[i] = dfu[j];
            else
              dfu[j] = dfu[i];
          }
  // Compute canonical form of the parametrization.
  for (uint n = 0; n < num_params_; n++) {
    if (dfu[n] != n) continue; // only for roots of components
    CharId chr = params[n];
    if (interchangable_[n][chr]) continue;
    // Precompute minimal set of positions such that...
    bool keep = false;
    set<MapId> maps;
    set<VarId> other_vars;
    for (uint p = 0; p < num_params_; p++) {
      if (dfu[p] == n) {
        if (params[p] != chr) keep = true;
        for (auto f: used_maps_[p]) {
          if (used_vars_.count(game_->getMappingValue(f, chr))) keep = true;
          maps.insert(f);
        }
      } else {
        for (auto f: used_maps_[p]) {
          other_vars.insert(game_->getMappingValue(f, params[p]));
        }
      }
    }
    if (keep) continue;
    keep = true;
    // Try substituing the whole group with other char (from 0).
    for (CharId a = 0; a < chr; a++) {
      // Is it equivalent?
      if (!CharsEquiv(maps, a, chr)) continue;
      // Doesn't it indroduce conflicting vars?
      keep = false;
      for (auto f: maps) {
        VarId var = game_->getMappingValue(f, a);
        if (other_vars.count(var)) keep = true;
      }
      if (!keep) {
        // Substitude the whole group.
        for (uint p = 0; p < num_params_; p++)
          if (dfu[p] == n) params[p] = a;
        break;
      }
    }
  }
  // If the canonical form is not yet present, add it.
  if (gen_params_basic_.count(params) == 0) {
    gen_params_basic_.insert(params);
    gen_stats_.ph2++;
    GenParamsGraphFilter();
  }
}

void Experiment::GenParamsGraphFilter() {
  auto& params = gen_params_;
  bliss::Stats stats;
  auto graph = CreateGraphForParams(gen_var_groups_, params);
  auto canonical = graph->permute(graph->canonical_form(stats, nullptr, nullptr));
  auto h = canonical->get_hash();
  if (gen_graphs_.count(h) > 0)
    return;
  gen_graphs_[h] = params;
  gen_stats_.ph3++;
  gen_params_final_.insert(params);
}

bliss::Digraph* Experiment::CreateGraphForParams(vec<uint>& groups,
                                                 vec<CharId>& params) {
  auto g = game_->CreateGraph();
  // Change color of var vertices according to 'groups'.
  for (auto var: game_->variables()) {
    uint group = std::numeric_limits<uint>::max() - groups[var->id()];
    g->change_color(2*var->id() - 2, group);
    g->change_color(2*var->id() - 1, group);
  }
  // Construct graphs for outcome formulas.
  for (auto outcome: outcomes_) {
    outcome->AddToGraph(*g, &params);
  }
  return g;
}