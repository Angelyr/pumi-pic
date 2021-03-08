#include <particle_structs.hpp>
#include "read_particles.hpp"
#include <cmath>

#ifdef PP_USE_CUDA
typedef Kokkos::CudaSpace DeviceSpace;
#else
typedef Kokkos::HostSpace DeviceSpace;
#endif
void finalize() {
  Kokkos::finalize();
  MPI_Finalize();
}

int comm_rank, comm_size;
//Structure adding functions
int testSCSs(lid_t num_elems, lid_t num_ptcls, kkLidView ppe,
            kkGidView element_gids, kkLidView particle_elements,
             PS::MTVs particle_info);
int testCSRs(lid_t num_elems, lid_t num_ptcls, kkLidView ppe,
             kkGidView element_gids, kkLidView particle_elements,
             PS::MTVs particle_info);

//Simple tests of construction
template <typename PS>
int testCounts(const char* name, PS* structure, lid_t num_elems, lid_t num_ptcls);
template <typename PS>
int testParticleExistence(const char* name, PS* structure, lid_t num_ptcls);
template <typename PS>
int setValues(const char* name, PS* structure);
template <typename PS>
int pseudoPush(const char* name, PS* structure);

//Functionality tests
template <typename PS>
int testRebuild(const char* name, PS* structure);
template <typename PS>
int testMigration(const char* name, PS* structure);
template <typename PS>
int testMetrics(const char* name, PS* structure);
template <typename PS>
int testCopy(const char* name, PS* structure);
template <typename PS>
int testSegmentComp(const char* name, PS* structure);

//Edge Case tests
template <typename PS>
int migrateToEmptyAndRefill(const char* name, PS* structure);

int main(int argc, char* argv[]) {
  Kokkos::initialize(argc, argv);
  MPI_Init(&argc, &argv);

  MPI_Comm_rank(MPI_COMM_WORLD, &comm_rank);
  MPI_Comm_size(MPI_COMM_WORLD, &comm_size);

  if (argc != 2) {
    if (!comm_rank)
      fprintf(stdout, "[ERROR] Format: %s <particle_file_prefix>\n", argv[0]);
    finalize();
    return 0;
  }

  char filename[256];
  sprintf(filename, "%s_%d.ptl", argv[1], comm_rank);
  //Local count of fails
  int fails = 0;
  {
    //General structure parameters
    lid_t num_elems;
    lid_t num_ptcls;
    kkLidView ppe;
    kkGidView element_gids;
    kkLidView particle_elements;
    PS::MTVs particle_info;
    readParticles(filename, num_elems, num_ptcls, ppe, element_gids,
                  particle_elements, particle_info);

    //Add SCS
    fails += testSCSs(num_elems, num_ptcls, ppe, element_gids,
                      particle_elements, particle_info);
    //Add CSR
    // fails += testCSRs(num_elems, num_ptcls, ppe, element_gids,
    //                     particle_elements, particle_info);

    //Cleanup
    ps::destroyViews<Types>(particle_info);
  }

  //Finalize and print failures
  int total_fails = 0;
  MPI_Reduce(&fails, &total_fails, 1, MPI_INT, MPI_SUM, 0, MPI_COMM_WORLD);
  finalize();
  if (comm_rank == 0) {
    if(total_fails == 0)
      printf("All tests passed\n");
    else
      printf("%d tests failed\n", total_fails);
  }
  return total_fails;
}

template <typename PS>
int runTests(const char* name, PS* structure, lid_t num_elems, lid_t num_ptcls) {
  int comm_size;
  MPI_Comm_size(MPI_COMM_WORLD, &comm_size);
  int fails = 0;
  fails += testCounts(name, structure, num_elems, num_ptcls);
  fails += testParticleExistence(name, structure, num_ptcls);
  fails += setValues(name, structure);
  Kokkos::fence();
  fails += pseudoPush(name, structure);
  Kokkos::fence();
  fails += testMetrics(name, structure);
  fails += testRebuild(name, structure);
  fails += testMigration(name, structure);
  fails += testCopy(name, structure);
  // fails += testSegmentComp(name, structure);
  // if (comm_size > 1)
  //   fails += migrateToEmptyAndRefill(name, structure);

  return fails;
}


int testSCSs(lid_t num_elems, lid_t num_ptcls, kkLidView ppe,
             kkGidView element_gids, kkLidView particle_elements,
             PS::MTVs particle_info) {
  int fails = 0;
  //Build SCS with C = 32, sigma = ne, V = 1024
  std::string test_name = "scs_C32_SMAX_V1024";
  typedef ps::SellCSigma<Types, MemSpace> SCS;
  try {
    lid_t maxC = 32;
    lid_t sigma = num_elems;
    lid_t V = 1024;
    Kokkos::TeamPolicy<ExeSpace> policy(4, maxC);
    ps::ParticleStructure<SCS>* s = new ps::ParticleStructure<SCS>(
      SCS(policy, sigma, V, num_elems, num_ptcls, ppe,
          element_gids, particle_elements, particle_info));

    runTests(test_name.c_str(), s, num_elems, num_ptcls);
    delete s;
  }
  catch(...) {
    fprintf(stderr, "[ERROR] Tests crashed for %s on rank %d\n",
            test_name.c_str(), comm_rank);
    ++fails;
  }
  return fails;
  //Build SCS with C = 32, sigma = 1, V = 10
  test_name = "scs_C32_S1_V10";
  try {
    lid_t maxC = 32;
    lid_t sigma = 1;
    lid_t V = 10;
    Kokkos::TeamPolicy<ExeSpace> policy(4, maxC);
    ps::ParticleStructure<SCS>* s = new ps::ParticleStructure<SCS>(
      SCS(policy, sigma, V, num_elems, num_ptcls, ppe,
          element_gids, particle_elements, particle_info));
    runTests(test_name.c_str(), s, num_elems, num_ptcls);
    delete s;
  }
  catch(...) {
    fprintf(stderr, "[ERROR] Tests crashed for %s on rank %d\n",
            test_name.c_str(), comm_rank);
    ++fails;
  }
  return fails;
}

int addCSRs(std::vector<PS*>& structures, std::vector<std::string>& names,
            lid_t num_elems, lid_t num_ptcls, kkLidView ppe,
            kkGidView element_gids, kkLidView particle_elements, PS::MTVs particle_info) {
  int fails = 0;
  // try {
  //   Kokkos::TeamPolicy<ExeSpace> policy(num_elems,32);
  //   PS* s = new ps::CSR<Types, MemSpace>(policy, num_elems, num_ptcls, ppe,
  //                                        element_gids, particle_elements, particle_info);
  //   structures.push_back(s);
  //   names.push_back("csr");
  // }
  // catch(...) {
  //   fprintf(stderr, "[ERROR] Construction of CSR failed on rank %d\n", comm_rank);
  //   ++fails;
  // }
  return fails;
}


template <typename PS>
int testCounts(const char* name, PS* structure, lid_t num_elems, lid_t num_ptcls) {
  int fails = 0;
  if (structure->nElems() != num_elems) {
    fprintf(stderr, "[ERROR] Test %s: Element count mismatch on rank %d "
            "[(structure)%d != %d(actual)]\n", name,
            comm_rank, structure->nElems(), num_elems);
    ++fails;
  }
  if (structure->nPtcls() != num_ptcls) {
    fprintf(stderr, "[ERROR] Test %s: Particle count mismatch on rank %d "
            "[(structure)%d != %d(actual)]\n", name,
            comm_rank, structure->nPtcls(), num_ptcls);
    ++fails;
  }
  if (structure->numRows() < num_elems) {
    fprintf(stderr, "[ERROR] Test %s: Number of rows is too small to fit elements on rank %d "
            "[(structure)%d < %d(actual)]\n", name, comm_rank,
            structure->numRows(), num_elems);
    ++fails;
  }
  if (structure->capacity() < num_ptcls) {
    fprintf(stderr, "[ERROR] Test %s: Capcity is too small to fit particles on rank %d "
            "[(structure)%d < %d(actual)]\n", name, comm_rank,
            structure->capacity(), num_ptcls);
    ++fails;
  }
  return fails;
}

template <typename PS>
int testParticleExistence(const char* name, PS* structure, lid_t num_ptcls) {
  int fails = 0;
  kkLidView count("count", 1);
  auto checkExistence = PS_LAMBDA(const lid_t& e, const lid_t& p, const bool& mask) {
    Kokkos::atomic_fetch_add(&(count(0)), mask);
  };
  ps::parallel_for(structure, checkExistence, "check particle existence");
  lid_t c = ps::getLastValue<lid_t>(count);
  if (c != num_ptcls) {
    fprintf(stderr, "[ERROR] Test %s: Number of particles found in parallel_for "
            "does not match the number of particles on rank %d"
            "[(parallel_for)%d != %d(actual)]]n", name, comm_rank,
            c, num_ptcls);
    ++fails;
  }
  return fails;
}

template <typename PS>
int setValues(const char* name, PS* structure) {
  int fails = 0;
  auto dbls = structure->template get<1>();
  auto bools = structure->template get<2>();
  auto nums = structure->template get<3>();
  int local_rank = comm_rank;
  auto setValues = PS_LAMBDA(const lid_t& e, const lid_t& p, const bool& mask) {
    if (mask) {
      dbls(p, 0) = p * e * 100.0;
      dbls(p, 1) = M_PI * p + M_PI / 2.0;
      dbls(p, 2) = M_E * 2.5;
      nums(p) = local_rank;
      bools(p) = true;
    }
    else {
      dbls(p, 0) = 0;
      dbls(p, 1) = 0;
      dbls(p, 2) = 0;
      nums(p) = -1;
      bools(p) = false;
    }
  };
  Kokkos::fence();
  Kokkos::Timer timer;
  ps::parallel_for(structure, setValues, "setValues");
  Kokkos::fence();
  double time = timer.seconds();
  printf("Time to set values %s : %f\n", name, time);
  return fails;
}

template <typename PS>
int pseudoPush(const char* name, PS* structure){
  int fails = 0;

  int elements = structure->nElems();
  fprintf(stderr, "elements : %d\n", elements);
  Kokkos::View<double*> parentElmData("parentElmData", elements);
  fprintf(stderr, "parent elm data size : %d\n", parentElmData.size());
  Kokkos::parallel_for("parentElmData", parentElmData.size(),
      KOKKOS_LAMBDA(const lid_t& e){
    parentElmData(e) = 2+3*e;
  });
  pumipic::printView(parentElmData);

  auto dbls = structure->template get<1>();
  auto bools = structure->template get<2>();
  auto nums = structure->template get<3>();
  int local_rank = comm_rank;
  auto quickMaths = PS_LAMBDA(const lid_t& e, const lid_t& p, const bool& mask){
    printf("e: %d\tp: %d\tmask: %d\n", e, p, mask);
    if(mask){
      dbls(p, 0) += 10;
      dbls(p, 1) += 10;
      dbls(p, 2) += 10;
      dbls(p, 0) = dbls(p,0) * dbls(p,0) * dbls(p,0) / sqrt(p*1.0) / sqrt(e*1.0) +
        parentElmData(e);
      dbls(p, 1) = dbls(p,1) * dbls(p,1) * dbls(p,1) / sqrt(p*1.0) / sqrt(e*1.0) +
        parentElmData(e);
      dbls(p, 2) = dbls(p,2) * dbls(p,2) * dbls(p,2) / sqrt(p*1.0) / sqrt(e*1.0) +
        parentElmData(e);
      nums(p) = local_rank;
      bools(p) = true;
    }
    else{
      dbls(p, 0) = 0;
      dbls(p, 1) = 0;
      dbls(p, 2) = 0;
      nums(p) = -1;
      bools(p) = false;
    }
  };

  Kokkos::fence();
  Kokkos::Timer timer;
  ps::parallel_for(structure, quickMaths, "setValues");
  Kokkos::fence();
  double time = timer.seconds();
  printf("Time for math Ops on %s : %f\n", name, time);

  return fails;
}

//Functionality tests
template <typename PS>
int testRebuild(const char* name, PS* structure) {
  int fails = 0;
  return fails;
}

template <typename PS>
int testMigration(const char* name, PS* structure) {
  int fails = 0;
  kkLidView failures("fails", 1);

  kkLidView new_element("new_element", structure->capacity());
  kkLidView new_process("new_process", structure->capacity());

  int num_ptcls = structure->nPtcls();
  //Send even particles one process to the right
  auto pids = structure->template get<0>();
  auto rnks = structure->template get<3>();
  int local_rank = comm_rank;
  int local_csize = comm_size;
  int num_elems = structure->nElems();
  auto sendRight = PS_LAMBDA(const lid_t e, const lid_t p, const bool mask) {
    if (mask) {
      new_element(p) = e;
      if (e == num_elems - 1) {
        new_process(p) = (local_rank + 1) % local_csize;
      }
      else
        new_process(p) = local_rank;
      rnks(p) = local_rank;
    }
    else {
      new_element(p) = e;
      new_process(p) = local_rank;
    }
  };
  ps::parallel_for(structure, sendRight, "sendRight");
  structure->migrate(new_element, new_process);

  pids = structure->template get<0>();
  rnks = structure->template get<3>();
  auto checkPostMigrate = PS_LAMBDA(const lid_t e, const lid_t p, const bool mask) {
    if (mask) {
      const int pid = pids(p);
      const int rank = rnks(p);
      if (e == num_elems - 1 && rank == local_rank) {
        printf("[ERROR] Failed to send particle %d on rank %d\n",
               pid, local_rank);
        failures(0) = 1;
      }
      if (e != 0 && rank != local_rank) {
        printf("[ERROR] Incorrectly received particle %d from rank %d to rank %d in element %d\n", pid, rank, local_rank, e);
        failures(0) = 1;
      }
    }
  };
  if (comm_size > 1)
    ps::parallel_for(structure, checkPostMigrate, "checkPostMigrate");
  fails += ps::getLastValue<lid_t>(failures);

  //Make a distributor
  int neighbors[3];
  neighbors[0] = comm_rank;
  neighbors[1] = (comm_rank - 1 + comm_size) % comm_size;
  neighbors[2] = (comm_rank + 1) % comm_size;
  ps::Distributor<typename PS::memory_space> dist(std::min(comm_size, 3), neighbors);

  new_element = kkLidView("new_element", structure->capacity());
  new_process = kkLidView("new_process", structure->capacity());
  auto sendBack = PS_LAMBDA(const lid_t e, const lid_t p, const bool mask) {
    new_element(p) = e;
    new_process(p) = rnks(p);
  };
  ps::parallel_for(structure, sendBack, "sendBack");
  structure->migrate(new_element, new_process, dist);

  failures = kkLidView("fails", 1);
  pids = structure->template get<0>();
  rnks = structure->template get<3>();
  auto checkPostBackMigrate = PS_LAMBDA(const lid_t e, const lid_t p, const bool mask) {
    if (mask) {
      if (rnks(p) != local_rank) {
        printf("[ERROR] Test %s: Particle %d from rank %d was not sent back on rank %d\n",
               name, pids(p), rnks(p), local_rank);
        failures(0) = 1;
      }
    }
  };
  ps::parallel_for(structure, checkPostBackMigrate, "checkPostBackMigrate");

  fails += ps::getLastValue<lid_t>(failures);
  if (num_ptcls != structure->nPtcls()) {
    printf("[ERROR] Test %s: Structure does not have all of the particles it started with on rank %d\n", name, comm_rank);
    ++fails;
  }

  return fails;
}

template <typename PS>
int testMetrics(const char* name, PS* structure) {
  int fails = 0;
  try {
    structure->printMetrics();
  }
  catch(...) {
    fprintf(stderr, "[ERROR] Test %s: Failed running printMetrics() on rank %d\n",
            name, comm_rank);
    ++fails;
  }
  return fails;
}

template <typename PS>
int testCopy(const char* name, PS* structure) {
  int fails = 0;
  //Copy particle structure to the host
  typename PS::Mirror<Kokkos::HostSpace>* host_structure =
    new typename PS::Mirror<Kokkos::HostSpace>(*structure);
  if (host_structure->nElems() != structure->nElems()) {
    fprintf(stderr, "[ERROR] Test %s: Failed to copy nElems() on rank %d\n",
            name, comm_rank);
    ++fails;
  }
  if (host_structure->nPtcls() != structure->nPtcls()) {
    fprintf(stderr, "[ERROR] Test %s: Failed to copy nPtcls() on rank %d\n",
            name, comm_rank);
    ++fails;
  }
  if (host_structure->capacity() != structure->capacity()) {
    fprintf(stderr, "[ERROR] Test %s: Failed to copy capacity() on rank %d\n",
            name, comm_rank);
    ++fails;
  }
  if (host_structure->numRows() != structure->numRows()) {
    fprintf(stderr, "[ERROR] Test %s: Failed to copy numRows() on rank %d\n",
            name, comm_rank);
    ++fails;
  }
  // //Copy particle structure back to the device
  PS* device_structure = new PS(*host_structure);
  delete host_structure;
  if (device_structure->nElems() != structure->nElems()) {
    fprintf(stderr, "[ERROR] Test %s: Failed to copy nElems() back to device on rank %d\n",
            name, comm_rank);
    ++fails;
  }
  if (device_structure->nPtcls() != structure->nPtcls()) {
    fprintf(stderr, "[ERROR] Test %s: Failed to copy nPtcls() back to device on rank %d\n",
            name, comm_rank);
    ++fails;
  }
  if (device_structure->capacity() != structure->capacity()) {
    fprintf(stderr, "[ERROR] Test %s: Failed to copy capacity() back to device on rank %d\n",
            name, comm_rank);
    ++fails;
  }
  if (device_structure->numRows() != structure->numRows()) {
    fprintf(stderr, "[ERROR] Test %s: Failed to copy numRows() back to device on rank %d\n",
            name, comm_rank);
    ++fails;
  }

  //Compare original and new particle structure on the device
  auto ids1 = structure->template get<0>();
  auto ids2 = device_structure->template get<0>();
  auto dbls1 = structure->template get<1>();
  auto dbls2 = device_structure->template get<1>();
  double EPSILON = .00001;
  kkLidView failure("failure", 1);
  int local_rank = comm_rank;
  auto testTypes = PS_LAMBDA(const int& eid, const int& pid, const bool mask) {
    if (mask) {
      if (ids1(pid) != ids2(pid)) {
        printf("[ERROR] Particle ids do not match for particle %d "
               "[(old) %d != %d (copy)] on rank %d\n", pid, ids1(pid),
               ids2(pid), local_rank);
        failure(0) = 1;
      }
      for (int i = 0; i < 3; ++i)
        if (fabs(dbls1(pid,i) - dbls2(pid,i)) > EPSILON) {
          printf("[ERROR] Particle's dbl %d does not match for particle %d"
                 "[(old) %.4f != %.4f (copy)] on rank %d\n", i, pid, dbls1(pid,i),
                 dbls2(pid,i), local_rank);
          failure(0) = 1;
      }
    }
  };
  ps::parallel_for(structure, testTypes, "testTypes on original structure");
  if (ps::getLastValue<lid_t>(failure)) {
    fprintf(stderr, "[ERROR] Test %s: Parallel for on original structure had failures\n",
            name);
    ++fails;
  }

  Kokkos::parallel_for(1, KOKKOS_LAMBDA(const int& i) {failure(i) = 0;});
  ps::parallel_for(device_structure, testTypes, "testTypes on copy of structure");
  if (ps::getLastValue<lid_t>(failure)) {
    fprintf(stderr, "[ERROR] Test %s: Parallel for on device structure had failures\n",
            name);
    ++fails;
  }
  delete device_structure;
  return fails;
}

template <typename PS>
int testSegmentComp(const char* name, PS* structure) {
  int fails = 0;
  kkLidView failures("fails", 1);

  auto dbls = structure->template get<1>();
  auto setComponents = PS_LAMBDA(const lid_t e, const lid_t p, const bool mask) {
    auto dbl_seg = dbls.getComponents(p);
    for (int i = 0; i < 3; ++i)
      dbl_seg(i) = e * (i + 1);
  };
  pumipic::parallel_for(structure, setComponents, "Set components");

  const double TOL = .00001;
  auto checkComponents = PS_LAMBDA(const lid_t e, const lid_t p, const bool mask) {
    auto comps = dbls.getComponents(p);
    for (int i = 0; i < 3; ++i) {
      if (abs(comps[i] - e * (i + 1)) > TOL) {
        printf("[ERROR] component is wrong on ptcl %d comp %d (%.3f != %d)\n",
               p, i, comps[i], e * (i + 1));
        Kokkos::atomic_add(&(failures[0]), 1);
      }
    }
  };
  pumipic::parallel_for(structure, checkComponents, "Check components");
  fails += pumipic::getLastValue<lid_t>(failures);

  return fails;
}

template <typename PS>
int migrateToEmptyAndRefill(const char* name, PS* structure) {
  int fails = 0;
  kkLidView failures("fails", 1);

  int originalPtcls = structure->nPtcls();

  kkLidView new_element("new_element", structure->capacity());
  kkLidView new_process("new_process", structure->capacity());

  int local_rank = comm_rank;
  int local_csize = comm_size;
  auto rnks = structure->template get<3>();
  auto elem = structure->template get<2>();
  int num_elems = structure->nElems();

  auto sendToOdd = PS_LAMBDA(lid_t e, lid_t p, bool mask) {
    rnks(p) = local_rank;
    elem(p) = e;
    if (mask) {
      if (local_rank % 2 == 0) {
        new_process(p) = (local_rank + 1) % local_csize;
        new_element(p) = num_elems - 1;
      }
      else {
        new_process(p) = local_rank;
        new_element(p) = e;
      }
    }
    else {
      new_element(p) = e;
      new_process(p) = local_rank;
    }
  };
  ps::parallel_for(structure, sendToOdd, "sendToOdd");
  structure->migrate(new_element, new_process);

  if (comm_rank % 2 == 0 && (comm_rank != 0 || comm_size % 2 == 0)) {
    if (structure->nPtcls() != 0) {
      ++fails;
      fprintf(stderr, "[ERROR] Particles remain on rank %d\n", comm_rank);
    }
    auto checkPtcls = PS_LAMBDA(lid_t e, lid_t p, bool mask) {
      if (mask) {
        failures(0) = 1;
        printf("[ERROR] Particle %d remains on rank %d\n", p, local_rank);
      }
    };
    ps::parallel_for(structure, checkPtcls, "checkPtcls");
  }
  else {
    if (structure->nPtcls() < originalPtcls) {
      ++fails;
      fprintf(stderr, "[ERROR] No particles on rank %d\n", comm_rank);
    }
    const int prev_rank = ((comm_rank - 1) + comm_size) % comm_size;
    auto new_rnks = structure->template get<3>();
    auto checkPtcls = PS_LAMBDA(lid_t e, lid_t p, bool mask) {
      if (mask) {
        if (new_rnks(p) != local_rank && new_rnks(p) !=  prev_rank) {
          failures(0) = 1;
          printf("[ERROR] Particle %d is not from ranks %d or %d\n", p, local_rank, prev_rank);
        }
      }
    };
    ps::parallel_for(structure, checkPtcls, "checkPtcls");
  }

  //Send Particles back to original process
  rnks = structure->template get<3>();
  new_element = kkLidView("new_element", structure->capacity());
  new_process = kkLidView("new_process", structure->capacity());
  auto sendToOrig = PS_LAMBDA(lid_t e, lid_t p, bool mask) {
    new_element(p) = e;
    if (mask) {
      new_process(p) = rnks(p);
    }
    else {
      new_process(p) = local_rank;
    }
  };
  ps::parallel_for(structure, sendToOrig, "sendToOrig");
  structure->migrate(new_element, new_process);

  if (structure->nPtcls() != originalPtcls) {
    ++fails;
    fprintf(stderr, "[ERROR] Number of particles does not match original on "
            "rank %d [%d != %d]\n", comm_rank, structure->nPtcls(), originalPtcls);
  }

  auto elems = structure->template get<2>();
  new_element = kkLidView("new_element", structure->capacity());
  auto resetElements = PS_LAMBDA(lid_t e, lid_t p, bool mask) {
    if (mask) {
      new_element(p) = elems(p);
    }
    else {
      new_element(p) = e;
    }
  };
  structure->rebuild(new_element);

  fails += pumipic::getLastValue<lid_t>(failures);
  return fails;
}
