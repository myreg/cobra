/*
 * Copyright 2014, Mirek Klimos <myreggg@gmail.com>
 */
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <set>
#include <iostream>
#include <cmath>
#include <ctime>
#include <bliss/graph.hh>
#include <bliss/utils.hh>
#include <tclap/CmdLine.h>
#include <sys/stat.h>
#include "formula.h"
#include "game.h"
#include "experiment.h"
#include "common.h"
#include "parser.h"
#include "strategy.h"
#include "pico-solver.h"
#include "simple-solver.h"

extern "C" int yyparse();
extern "C" FILE* yyin;
extern Parser m;

std::function<uint(vec<Option>&)> g_breakerStg;
std::function<uint(Option&)> g_makerStg;

typedef struct Args{
  string filename;
  string mode;
  string backend;
  string stg_experiment;
  string stg_outcome;
} Args;

Args args;

Solver* get_solver(const vec<Variable*>& vars, Formula* restriction = nullptr) {
  if (args.backend == "picosat") {
    return new PicoSolver(vars, restriction);
  } else if (args.backend == "simple") {
    return new SimpleSolver(vars, restriction);
  }
  assert(false);
}

void print_head(string name) {
  printf("\n%s===== %s =====%s\n", color::shead, name.c_str(), color::snormal);
}

void time_overview(clock_t start) {
  print_head("TIME OVERVIEW");
  printf("Total time: %.2fs\n", toSeconds(clock() - start));
  printf("Bliss (calls/time): %u/%.2fs\n",
         m.game().bliss_calls(), toSeconds(m.game().bliss_time()));
  auto s1 = PicoSolver::s_stats();
  printf("PicoSolver (calls/time): sat %i/%.2fs fixed %i/%.2fs models %i/%.2fs\n",
    s1.sat_calls, toSeconds(s1.sat_time),
    s1.fixed_calls, toSeconds(s1.fixed_time),
    s1.models_calls, toSeconds(s1.models_time));
  auto s2 = SimpleSolver::s_stats();
  printf("SimpleSolver (calls/time): sat %i/%.2fs fixed %i/%.2fs models %i/%.2fs\n",
    s2.sat_calls, toSeconds(s2.sat_time),
    s2.fixed_calls, toSeconds(s2.fixed_time),
    s2.models_calls, toSeconds(s2.models_time));
}

void overview_mode() {
  Game& game = m.game();
  print_head("GAME OVERVIEW");
  printf("Num of variables: %lu\n", game.vars().size());
  Solver* solver = get_solver(game.vars(), game.restriction());
  uint models = solver->NumOfModels();
  printf("Num of possible codes: %u\n\n", models);

  struct stat file_st;
  stat(args.filename.c_str(), &file_st);
  uint branching = 0, num_exp = 0, nodes = 0;
  for (auto e: game.experiments()) {
    for (auto f: e->outcomes()) {
      nodes += f.formula->Size();
    }
    if (e->outcomes().size() > branching) {
      branching = e->outcomes().size();
    }
    num_exp += e->NumberOfParametrizations();
  }

  printf("Read from file: %s\n", args.filename.c_str());
  printf("File size: %lli\n", file_st.st_size);
  printf("Num of nodes in formulas: %u\n\n", nodes);


  printf("Num of types of experiments: %lu\n", game.experiments().size());
  printf("Alphabet size: %lu\n", game.alphabet().size());
  printf("Total num of experiments: %u\n", num_exp);
  printf("Avg num of parametrizations per type: %.2f\n",
         (float)num_exp/game.experiments().size());
  printf("Maximal branching: %u\n", branching);
  double d = log(models)/log(branching);
  printf("Trivial lower bound (expected): %.2f\n", d);
  printf("Trivial lower bound (worst-case): %.0f\n\n", ceil(d));

  printf("Well-formed check...");
  fflush(stdout);

  auto t1 = clock();
  game.Precompute();
  auto knowledge_graph = game.CreateGraph();
  game.restriction()->AddToGraph(*knowledge_graph, nullptr);
  auto var_equiv = game.ComputeVarEquiv(*solver, *knowledge_graph);
  for (auto e: game.experiments()) {
    auto& params_all = e->GenParams(var_equiv);
    for (auto& params: params_all) {
      auto f1 = new vec<Formula*>();
      for (auto o: e->outcomes()) f1->push_back(o.formula);
      auto f2 = m.get<ExactlyOperator>(1, f1);
      auto f3 = m.get<NotOperator>(f2);

      solver->OpenContext();
      solver->AddConstraint(f3, params);
      if (solver->Satisfiable()) {
        printf("%s failed!%s\n", color::serror, color::snormal);
        printf("EXPERIMENT: %s %s", e->name().c_str(), game.ParamsToStr(params).c_str());
        printf("\nPROBLEMATIC ASSIGNMENT: \n");
        solver->PrintAssignment();
        printf("\n");
        return;
      }
      solver->CloseContext();
    }
  }

  delete solver;
  auto t2 = clock();
  printf("ok [%.2fs]\n", double(t2 - t1)/CLOCKS_PER_SEC);
}

void simulation_mode() {
  print_head("SIMULATION");
  Game& game = m.game();
  game.Precompute();
  auto knowledge_graph = game.CreateGraph();
  game.restriction()->AddToGraph(*knowledge_graph, nullptr);

  Solver* solver = get_solver(game.vars(), game.restriction());

  int exp_num = 1;
  while (true) {
    auto options = game.GenerateExperiments(*solver, *knowledge_graph);

    // Choose and print an experiment
    auto experiment = options[g_breakerStg(options)];
    printf("%sEXPERIMENT: %s %s %s\n",
           color::semph,
           experiment.type().name().c_str(),
           game.ParamsToStr(experiment.params()).c_str(),
           color::snormal);

    // Choose and print an outcome
    uint oid = g_makerStg(experiment);
    assert(oid < experiment.type().outcomes().size());
    assert(experiment.IsSat(oid) == true);
    auto outcome = experiment.type().outcomes()[oid];
    printf("%sOUTCOME: %s\n",
           color::semph,
           outcome.name.c_str());
    auto knowledge = outcome.formula->pretty(true, &experiment.params());
    if (knowledge.length() > 200) knowledge = knowledge.substr(0, 200) + "...";
    printf("  ->   %s\n\n%s",
           knowledge.c_str(),
           color::snormal);

    // Check if solved.
    solver->AddConstraint(outcome.formula, experiment.params());
    auto sat = solver->Satisfiable();
    assert(sat);
    auto assingment = solver->GetAssignment();
    if (solver->OnlyOneModel()) {
      printf("%sSOLVED in %i experiments!%s\n", color::shead, exp_num, color::snormal);
      game.PrintCode(assingment);
      break;
    }

    // Prepare for another round.
    exp_num++;
    outcome.formula->AddToGraph(*knowledge_graph, &experiment.params());
  }
  delete knowledge_graph;
}

void analyze(Solver& solver, bliss::Digraph& graph, uint depth, uint& max, uint& sum, uint& num) {
  Game& game = m.game();
  auto options = game.GenerateExperiments(solver, graph);
  auto x = g_breakerStg(options);
  assert(x < options.size());
  auto experiment = options[x];
  for (uint i = 0; i < experiment.type().outcomes().size(); i++) {
    solver.OpenContext();
    auto outcome = experiment.type().outcomes()[i];
    solver.AddConstraint(outcome.formula, experiment.params());
    bool sat = solver.Satisfiable(), one = false;
    if (sat) one = solver.OnlyOneModel();
    if (one) {
      num += 1;
      printf("\b\b\b\b\b%5u", num);
      fflush(stdout);
      auto finaldepth = outcome.last ? depth : depth + 1;
      sum += finaldepth;
      max = std::max(max, finaldepth);
    } else if (sat) {
      bliss::Digraph ngraph(graph);
      outcome.formula->AddToGraph(ngraph, &experiment.params());
      analyze(solver, ngraph, depth + 1, max, sum, num);
    }
    solver.CloseContext();
  }
}

void analyze_mode() {
  print_head("STRATEGY ANALYSIS");
  Game& game = m.game();
  game.Precompute();
  auto graph = game.CreateGraph();
  game.restriction()->AddToGraph(*graph, nullptr);
  Solver* solver = get_solver(game.vars(), game.restriction());
  uint models = solver->NumOfModels();
  uint max = 0, sum = 0, num = 0;
  printf("Codes found (total %u):     0", models);
  fflush(stdout);
  // TODO: jeste bych mohl vypisovat nejake ETA, ale mozna neni potreba
  analyze(*solver, *graph, 1, max, sum, num);
  delete solver;
  delete graph;
  printf("\nWorst-case: %u\n", max);
  printf("Average-case: %.4f (%u/%u)\n", (double)sum/models, sum, models);
}

// Parse program arguments with TCLAP library.
void parse_args(int argc, char* argv[]) {
  using namespace TCLAP;
  CmdLine cmd("Code Breaking Game Analyzer blah blah blah", ' ', "0.1");

  vec<string> modes = { "o", "overview", "s", "simulation", "a", "analysis", "o", "optimal" };
  ValuesConstraint<string> modeConstraint(modes);
  ValueArg<string> modeArg(
    "m", "mode",
    "Mode of operation. Default: overview.", false,
    "o", &modeConstraint);

  vec<string> backends = { "picosat", "simple" };
  ValuesConstraint<string> backendConstraint(backends);
  ValueArg<string> backendArg(
    "b", "backend",
    "Specifies SAT solver that will be used. Default: picosat.", false,
    "simple", &backendConstraint);

  vec<string> e_stgs, o_stgs;
  string e_man = "", o_man = "";
  for (auto s: strategy::breaker_strategies) {
    e_stgs.push_back(s.first);
    e_man += color::emph + s.first + ": " + color::normal + s.second.first;
  }
  for (auto s: strategy::maker_strategies) {
    o_stgs.push_back(s.first);
    o_man += color::emph + s.first + ": " + color::normal + s.second.first;
  }
  ValuesConstraint<string> o_constr(o_stgs);
  ValueArg<string> o_arg(
    "o", "codemaker",
    "Strategy that selects an outcome, played by the codemaker. Default: interactive." + o_man, false,
    "interactive", &o_constr);
  ValuesConstraint<string> e_constr(e_stgs);
  ValueArg<string> e_arg(
    "e", "codebreaker",
    "Strategy that selects an experiment, played by the codebreaker. Default: interactive." + e_man, false,
    "interactive", &e_constr);


  UnlabeledValueArg<std::string> filenameArg(
    "filename",
    "Input file name.", false,
    "", "file name");

  cmd.add(e_arg);
  cmd.add(o_arg);
  cmd.add(backendArg);
  cmd.add(modeArg);
  cmd.add(filenameArg);
  cmd.parse(argc, argv);

  args.filename = filenameArg.getValue();
  args.mode = modeArg.getValue();
  args.backend = backendArg.getValue();
  args.stg_experiment = e_arg.getValue();
  args.stg_outcome = o_arg.getValue();
}

int main(int argc, char* argv[]) {
  srand(time(NULL));
  try {
    parse_args(argc, argv);

    if (!(yyin = fopen(args.filename.c_str(), "r"))) {
      printf("Cannot open %s: %s.\n", args.filename.c_str(), strerror(errno));
      exit(EXIT_FAILURE);
    }
    auto t1 = clock();
    printf("Loading... ");
    try {
      yyparse();
    } catch (ParserException* p) {
      printf("\nInvalid input: %s\n", p->what());
      exit(EXIT_FAILURE);
    }
    fclose (yyin);
    auto t2 = clock();
    printf("[%.2fs]\n", double(t2 - t1)/CLOCKS_PER_SEC);
    t1 = clock();

    g_breakerStg = strategy::breaker_strategies.at(args.stg_experiment).second;
    g_makerStg = strategy::maker_strategies.at(args.stg_outcome).second;

    if (args.mode == "o" || args.mode == "overview") {
      overview_mode();
    } else if (args.mode == "s" || args.mode == "simulation") {
      simulation_mode();
    } else if (args.mode == "a" || args.mode == "analysis") {
      if (args.stg_experiment == "interactive" || args.stg_experiment == "random") {
        printf("Cannot analyze strategy '%s'. \n", args.stg_experiment.c_str());
        exit(EXIT_FAILURE);
      }
      analyze_mode();
    }

    time_overview(t1);
    m.deleteAll();
  } catch (TCLAP::ArgException &e) {
    std::cerr << "Error: " << e.error() << " for arg " << e.argId() << std::endl;
  }
  return 0;
}