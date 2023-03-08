#include <particle_structs.hpp>
#include <ppTiming.hpp>
#include <Kokkos_Random.hpp>
#include "perfTypes.hpp"
#include "../particle_structs/test/Distribute.h"
#include <string>

const char* usage = "Usage argument: ./ps_combo160 num_elms num_ptcls distribution structure_type\n[-p percentMovedRebuild] [-pp percentMovedMigrate] [-s team_size] [-v vertical_slicing] [--optimal]";

typedef std::map<int,std::string> mis;
const mis StructIdxToString = {{0, "SCS"}, {1, "CSR"}, {2, "CabM"}, {3, "DPS"}};

void structure_help() {
  printf("\nAvailable particle structures:\n");
  for(auto ps : StructIdxToString)
    printf("%d - %s\n", ps.first, ps.second.c_str());
}

void printHelpAndExit() {
  fprintf(stderr, "%s\n", usage);
  structure_help();
  distribute_help();
  Kokkos::finalize();
  exit(EXIT_FAILURE);
}

struct PSOptions {
  std::string size;
  int structure;
  int strat;
  int num_elems;
  int num_ptcls;
  int ppe;
  int team_size;
  int vert_slice;
  bool optimal;
};

template<typename DataTypes>
pumipic::ParticleStructure<DataTypes, MemSpace>* createParticleStruct(PSOptions& options,
    kkLidView ppe, kkLidView ptcl_elems, kkGidView element_gids) {
  /* Create particle structure */
  if (options.structure == 0) {
    if (options.optimal) {
      if (options.strat == 1) {
        options.team_size = 512;
        options.vert_slice = 8;
      }
      else if (options.strat == 2) {
        options.team_size = 512;
        options.vert_slice = 4;
      }
      else if (options.strat == 3) {
        options.team_size = 128;
        options.vert_slice = 8;
      }
    }
    std::string name("Sell-"+std::to_string(options.team_size)+"-ne");
    return createSCS<DataTypes>(options.num_elems, options.num_ptcls, ppe, element_gids,
        options.team_size, options.num_elems, options.vert_slice, name);
  }
  else if (options.structure == 1) {
    std::string name("CSR");
    return createCSR<DataTypes>(options.num_elems, options.num_ptcls, ppe, element_gids, options.team_size);
  }
  else if (options.structure == 2) {
    std::string name("CabM");
#ifdef PP_ENABLE_CAB
    return createCabM<DataTypes>(options.num_elems, options.num_ptcls, ppe, element_gids, options.team_size, name);
#else
    fprintf(stderr, "CabM requested, but PUMI-PIC was not built with Cabana enabled\n");
#endif
  }
  else if (options.structure == 3) {
    std::string name("DPS");
#ifdef PP_ENABLE_CAB
    return createDPS<DataTypes>(options.num_elems, options.num_ptcls, ppe, element_gids, options.team_size, name);
#else
    fprintf(stderr, "DPS requested, but PUMI-PIC was not built with Cabana enabled\n");
#endif
  }
  else {
    exit(EXIT_FAILURE);
    return createSCS<DataTypes>(options.num_elems, options.num_ptcls, ppe, element_gids,
        options.team_size, options.num_elems, options.vert_slice, "foo");
  }
}

int main(int argc, char* argv[]) {
  Kokkos::initialize(argc, argv);
  MPI_Init(&argc, &argv);

  PSOptions options;
  // Default values if not specified on command line
  double percentMoved = 0.5;
  double percentMovedProcess = 0.1;
  options.team_size = 32;
  options.vert_slice = 1024;

  /* Check commandline arguments */
  // Required arguments
  if(argc < 5) printHelpAndExit();
  options.num_elems = atoi(argv[1]);
  options.num_ptcls = atoi(argv[2]);
  options.strat = atoi(argv[3]);
  options.structure = atoi(argv[4]);
  options.optimal = false;
  options.size = "small";

  // Optional arguments specified with flags
  for (int i = 5; i < argc; i+=2) {
    // -p = percent_moved
    if (std::string(argv[i]) == "-p") {
      percentMoved = atof(argv[i+1]);
    }
    // -pp = percent_moved_to_new_process
    else if (std::string(argv[i]) == "-pp") {
      percentMovedProcess = atof(argv[i+1]);
    }
    // -s = team_size (/chunk width)
    else if (std::string(argv[i]) == "-s") {
      options.team_size = atoi(argv[i+1]);
    }
    // -v = vertical slicing
    else if (std::string(argv[i]) == "-v") {
      options.vert_slice = atoi(argv[i+1]);
    }
    else if (std::string(argv[i]) == "--optimal") {
      options.optimal = true;
      i--;
    }
    else {
      fprintf(stderr, "Illegal argument: %s", argv[i]);
      printHelpAndExit();
    }
  }

  int comm_rank; // get process rank
  MPI_Comm_rank(MPI_COMM_WORLD, &comm_rank);
  int comm_size; // get number of processes
  MPI_Comm_size(MPI_COMM_WORLD, &comm_size);

  if (!comm_rank) {
    fprintf(stderr, "Test Command:\n");
    for (int i = 0; i < argc; i++) {
      fprintf(stderr, " %s", argv[i]);
    }
    fprintf(stderr, "\n");
  }

  /* Enable timing on every process */
  pumipic::SetTimingVerbosity(0);
  pumipic::enable_prebarrier();

  { // Begin Kokkos region

    /* Create initial distribution of particles */
    kkLidView ppe("ptcls_per_elem", options.num_elems);
    kkLidView ptcl_elems("ptcl_elems", options.num_ptcls);
    kkGidView element_gids("element_gids", options.num_elems);
    Kokkos::parallel_for(options.num_elems, KOKKOS_LAMBDA(const int i) { // set gids, sharing between all processes
        element_gids(i) = i;
    });
    if (!comm_rank)
      printf("Generating particle distribution with strategy: %s\n", distribute_name(options.strat));
    distribute_particles(options.num_elems, options.num_ptcls, options.strat, ppe, ptcl_elems);

    std::string name;
    auto ptcls = createParticleStruct<PerfTypes160>(options, ppe, ptcl_elems, element_gids);

    const int PS_ITERS = 100;
    const int ITERS = 100;
    
    if (!comm_rank)
      printf("Performing %d iterations of push on each structure\n", PS_ITERS);

    /* Perform push & rebuild on the particle structures */
    if (!comm_rank)
      printf("Beginning push on structure %s\n", name.c_str());

    // Per element data to access in pseudoPush
    Kokkos::View<double*> parentElmData("parentElmData", ptcls->nElems());
    Kokkos::parallel_for("parent_elem_data", parentElmData.size(),
        KOKKOS_LAMBDA(const int& e){
      parentElmData(e) = std::sqrt((double)e) * e;
    });

    for (int i = 0; i < PS_ITERS; ++i) {
      /* Begin Push Setup */
      auto dbls = ptcls->get<0>();
      auto nums = ptcls->get<1>();
      auto lint = ptcls->get<2>();

      auto pseudoPush = PS_LAMBDA(const int& e, const int& p, const bool& mask) {
        if (mask) {
          for (int i = 0; i < 17; i++) {
            dbls(p,i) = 10.3;
            dbls(p,i) = dbls(p,i) * dbls(p,i) * dbls(p,i) / std::sqrt((double)p) / std::sqrt((double)e) + parentElmData(e);
          }
          for (int i = 0; i < 4; i++) {
            nums(p,i) = 4*p + i;
          }
          lint(p) = p;
        }
        else {
          for (int i = 0; i < 17; i++) {
            dbls(p,i) = 0;
          }
          for (int i = 0; i < 4; i++) {
            nums(p,i) = -1;
          }
          lint(p) = 0;
        }
      };

      Kokkos::fence();
      Kokkos::Timer pseudo_push_timer;
      /* Begin push operations */
      ps::parallel_for(ptcls,pseudoPush,"pseudo push");
      Kokkos::fence();
      /* End push */
      float pseudo_push_time = pseudo_push_timer.seconds();
      pumipic::RecordTime(name+" pseudo-push", pseudo_push_time);
    }

    int seed = 0; // set seed for uniformly random processes
    Kokkos::Random_XorShift64_Pool<Kokkos::DefaultExecutionSpace> pool(seed);

    int* other_ranks = new int[comm_size-1];
    int counter = 0;
    for (int i = 0; i < comm_size; i++) {
      if (i != comm_rank)
        other_ranks[counter++] = i;
    }
    kkLidView other_ranks_d("other_ranks_d", comm_size-1);
    pumipic::hostToDevice(other_ranks_d, other_ranks);

    if (!comm_rank)
      printf("Performing %d iterations of migrate/rebuild on each structure\n", ITERS);

    if (!comm_rank)
      printf("Beginning migrate on structure %s\n", name.c_str());
    for (int i = 0; i < ITERS; ++i) {
      kkLidView new_elms("new elems", ptcls->capacity());
      Kokkos::Timer t;
      redistribute_particles(ptcls, options.strat, percentMoved, new_elms);
      pumipic::RecordTime("redistribute", t.seconds());

      Kokkos::Timer tp;
      kkLidView new_process("new_process", ptcls->capacity());
      if (comm_size > 1) {
        auto to_new_processes = PS_LAMBDA(const int& e, const int& p, const bool& mask) {
          if (mask) {
            auto generator = pool.get_state();
            double prob = generator.drand(1.0);
            if (prob < percentMovedProcess)
              new_process(p) = other_ranks_d( generator.urand(comm_size-1) ); // send particle to a different process
            else
              new_process(p) = comm_rank;
            pool.free_state(generator);
          }
        };
        pumipic::parallel_for(ptcls, to_new_processes, "to_new_processes");
      }
      pumipic::RecordTime("redistribute processes", tp.seconds());

      ptcls->migrate(new_elms, new_process);
    }

    delete ptcls;

  } // end Kokkos region

  pumipic::SummarizeTime();
  Kokkos::finalize();
  return 0;
}
