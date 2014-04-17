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

void time_overview() {
  printf("= Time overview = \n");
  auto s1 = PicoSolver::s_stats();
  printf("PicoSolver: sat %i [%.2f] fixed %i [%.2f] models %i [%.2f]\n",
    s1.sat_calls, toSeconds(s1.sat_time),
    s1.fixed_calls, toSeconds(s1.fixed_time),
    s1.models_calls, toSeconds(s1.models_time));
  auto s2 = SimpleSolver::s_stats();
  printf("SimpleSolver: sat %i [%.2f] fixed %i [%.2f] models %i [%.2f]\n",
    s2.sat_calls, toSeconds(s2.sat_time),
    s2.fixed_calls, toSeconds(s2.fixed_time),
    s2.models_calls, toSeconds(s2.models_time));
}

void print_stats(Game& game, string filename) {
  printf("\n%s===== GAME STATISTICS =====%s\n", color::shead, color::snormal);
  printf("Num of variables: %lu\n", game.variables().size());
  SimpleSolver s(game.variables(), game.restriction());
  Solver& solver = s;
  uint models = solver.NumOfModels();
  printf("Num of possible codes: %u\n\n", models);

  struct stat file_st;
  stat(filename.c_str(), &file_st);
  uint branching = 0, num_exp = 0, nodes = 0;
  for (auto e: game.experiments()) {
    for (auto f: e->outcomes()) {
      nodes += f->Size();
    }
    if (e->outcomes().size() > branching) {
      branching = e->outcomes().size();
    }
    num_exp += e->NumberOfParametrizations();
  }

  printf("Read from file: %s\n", filename.c_str());
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
  auto var_equiv = game.ComputeVarEquiv(solver, *knowledge_graph);
  for (auto e: game.experiments()) {
    auto& params_all = e->GenParams(var_equiv);
    for (auto& params: params_all) {
      auto f1 = new vec<Formula*>(e->outcomes().begin(), e->outcomes().end());
      auto f2 = m.get<ExactlyOperator>(1, f1);
      auto f3 = m.get<ImpliesOperator>(game.restriction(), f2);
      auto f4 = m.get<NotOperator>(f3);

      solver.OpenContext();
      solver.AddConstraint(f4, params);
      if (solver.Satisfiable()) {
        printf("%s failed!%s\n", color::serror, color::snormal);
        printf("EXPERIMENT: %s ", e->name().c_str());
        game.printParams(params);
        printf("\nPROBLEMATIC ASSIGNMENT: \n");
        solver.PrintAssignment();
        printf("\n");
        return;
      }
    }
  }
  auto t2 = clock();
  printf("ok [%.2fs]\n", double(t2 - t1)/CLOCKS_PER_SEC);
}

void simulation_mode() {
  Game& game = m.game();
  game.Precompute();
  printf("\n%s===== SIMULATION MODE =====%s\n\n", color::shead, color::snormal);
  auto knowledge_graph = game.CreateGraph();
  game.restriction()->AddToGraph(*knowledge_graph, nullptr);

  PicoSolver knowledge(game.variables(), game.restriction());
  //knowledge.WriteDimacs(stdout);

  int exp_num = 1;
  while (true) {
    auto options = game.GenerateExperiments(knowledge, *knowledge_graph);

    // Choose and print an experiment
    auto experiment = options[g_breakerStg(options)];
    printf("%sEXPERIMENT: %s ",
           color::semph,
           experiment.type().name().c_str());
    game.printParams(experiment.params());
    printf("%s\n", color::snormal);

    // Choose and print an outcome
    uint oid = g_makerStg(experiment);
    assert(oid < experiment.type().outcomes().size());
    assert(experiment.IsOutcomeSat(oid) == true);
    auto outcome = experiment.type().outcomes()[oid];
    printf("%sOUTCOME: %s\n",
           color::semph,
           experiment.type().outcomes_names()[oid].c_str());
    printf("  ->     %s\n\n%s",
           outcome->pretty(true, &experiment.params()).c_str(),
           color::snormal);

    // Check if solved.
    knowledge.AddConstraint(outcome, experiment.params());
    if (knowledge.NumOfModels() == 1) {
      printf("%sSOLVED in %i experiments!%s\n", color::shead, exp_num, color::snormal);
      knowledge.Satisfiable();
      knowledge.PrintAssignment();
      break;
    }

    // Prepare for another round.
    exp_num++;
    //knowledge.WriteDimacs(stdout);
    outcome->AddToGraph(*knowledge_graph, &experiment.params());
  }
}

void analyze_mode() {
  printf("Not implemented yet.\n");
}

int main(int argc, char* argv[]) {
  // Parse input with TCLAP library.
  srand(time(NULL));
  try {
    using namespace TCLAP;
    CmdLine cmd("Code Breaking Game Analyzer blah blah blah", ' ', "0.1");
    SwitchArg infoArg(
      "i", "info",
      "Print basic information about the game.",
      cmd, false);
    SwitchArg simArg(
      "s", "simulation",
      "Starts simulation mode, player strategies can be specified by -b, -m.",
      cmd, false);
    SwitchArg analyzeArg(
      "a", "analyze",
      "Analyze codebreaker's strategy.",
      cmd, false);

    vec<string> breaker_stgs, maker_stgs;
    string breaker_man = "", maker_man = "";
    for (auto s: strategy::breaker_strategies) {
      breaker_stgs.push_back(s.first);
      breaker_man += "\n" + color::emph + s.first + ": " + color::normal + s.second.first;
    }
    for (auto s: strategy::maker_strategies) {
      maker_stgs.push_back(s.first);
      maker_man += "\n" + color::emph + s.first + ": " + color::normal + s.second.first;
    }

    ValuesConstraint<string> makerConstraint(maker_stgs);
    ValueArg<string> codemakerArg(
      "1", "codemaker",
      "Strategy to be played by the codemaker. Default: interactive." + maker_man, false,
      "interactive", &makerConstraint);
    ValuesConstraint<string> breakerConstraint(breaker_stgs);
    ValueArg<string> codebreakerArg(
      "2", "codebreaker",
      "Strategy to be played by the codebreaker. Default: interactive." + breaker_man, false,
      "interactive", &breakerConstraint);
    cmd.add(codemakerArg);
    cmd.add(codebreakerArg);

    UnlabeledValueArg<std::string> filenameArg(
      "filename",
      "Input file name.", false,
      "", "file name");
    cmd.add(filenameArg);
    cmd.parse(argc, argv);

    auto file = filenameArg.getValue();
    if (!(yyin = fopen(file.c_str(), "r"))) {
      printf("Cannot open %s: %s.\n", file.c_str(), strerror(errno));
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

    if (infoArg.getValue()) {
      print_stats(m.game(), file);
    }

    if (simArg.getValue()) {
      g_breakerStg = strategy::breaker_strategies.at(codebreakerArg.getValue()).second;
      g_makerStg = strategy::maker_strategies.at(codemakerArg.getValue()).second;
      simulation_mode();
    }

    if (analyzeArg.getValue()) {
      if (codebreakerArg.getValue() == "interactive") {
        printf("Cannot analyze strategy 'interactive'. \n");
        exit(EXIT_FAILURE);
      }
      g_breakerStg = strategy::breaker_strategies.at(codebreakerArg.getValue()).second;
      analyze_mode();
    }

    time_overview();
  } catch (TCLAP::ArgException &e) {
    std::cerr << "Error: " << e.error() << " for arg " << e.argId() << std::endl;
  }

  return 0;
}