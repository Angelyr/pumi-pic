#pragma once
#include <ppTiming.hpp>
#include <adios2.h>

namespace pumipic {
  template <class DataTypes, typename MemSpace>
  void CabM<DataTypes, MemSpace>::checkpointWrite(std::string path) {
    const auto btime = prebarrier();
    Kokkos::Profiling::pushRegion("CabM checkpointWrite");
    Kokkos::Timer overall_timer;

    int comm_rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &comm_rank);
    if (!comm_rank) {
      fprintf(stderr, "doing adios2 stuff in cabm checkpointWrite\n");

      std::string config = "../../pumi-pic/pumipic-data/checkpoint/adios2.xml";
      adios2::ADIOS adios(config, MPI_COMM_WORLD);
      adios2::IO io = adios.DeclareIO("writerIO");

      adios2::Engine engine = io.Open(path, adios2::Mode::Write);

      // single-element variables saving
      
      adios2::Variable<lid_t> var_num_elems = io.DefineVariable<lid_t>("num_elems");
      adios2::Variable<lid_t> var_num_ptcls = io.DefineVariable<lid_t>("num_ptcls");
      adios2::Variable<lid_t> var_padding_start = io.DefineVariable<lid_t>("padding_start");
      adios2::Variable<double> var_extra_padding = io.DefineVariable<double>("extra_padding");
      adios2::Variable<std::string> var_name = io.DefineVariable<std::string>("name");

      // array-length variable saving
      adios2::Dims shape_elms{static_cast<size_t>(num_elems)};
      adios2::Dims start_elms{0};
      adios2::Dims count_elms{static_cast<size_t>(num_elems)};
      adios2::Variable<lid_t> var_ppe = io.DefineVariable<lid_t>("particles_per_element", shape_elms, start_elms, count_elms);
      adios2::Variable<gid_t> var_gids = io.DefineVariable<gid_t>("element_gids", shape_elms, start_elms, count_elms);
      //adios2::Dims shape_ptcls{static_cast<size_t>(num_ptcls)};
      //adios2::Dims start_ptcls{0};
      //adios2::Dims count_ptcls{static_cast<size_t>(num_ptcls)};
      //adios2::Variable<lid_t> var_ptcl_elems = io.DefineVariable<lid_t>("particle_elements", shape_ptcls, start_ptcls, count_ptcls);

      // go through AoSoA, constructing particles_per_element and particle_elements by counting number of active ptcls
      kkLidView particles_per_element_d("particles_per_element_d", num_elems);
      kkLidView particle_elements_d("particle_elements_d", num_ptcls);

      kkLidView parentElms_cpy = parentElms_;
      const auto soa_len = AoSoA_t::vector_length;
      const auto activeSliceIdx = aosoa_->number_of_members-1;
      auto active = Cabana::slice<activeSliceIdx>(*aosoa_);
      auto atomic = KOKKOS_LAMBDA(const lid_t& soa, const lid_t& tuple) {
        if (active.access(soa,tuple)) {
          lid_t elm = parentElms_cpy(soa);
          // count particles in each element
          Kokkos::atomic_increment<lid_t>(&particles_per_element_d(elm));
          particle_elements_d(soa*soa_len+tuple) = elm;
        }
      };
      Cabana::SimdPolicy<soa_len,execution_space> simd_policy(0, capacity_);
      Cabana::simd_parallel_for(simd_policy, atomic, "atomic");

      kkLidHostMirror particles_per_element_h = deviceToHost(particles_per_element_d);
      kkLidHostMirror particle_elements_h = deviceToHost(particle_elements_d);
      kkGidHostMirror element_to_gid_h = deviceToHost(element_to_gid);

      // TODO: AoSoA variable saving

      engine.BeginStep();
      engine.Put(var_num_elems, num_elems);
      engine.Put(var_num_ptcls, num_ptcls);
      engine.Put(var_padding_start, padding_start);
      engine.Put(var_extra_padding, extra_padding);
      engine.Put(var_name, name);

      engine.Put(var_ppe, particles_per_element_h.data());
      //engine.Put(var_ptcl_elems, particle_elements_h.data());
      engine.Put(var_gids, element_to_gid_h.data());
      engine.EndStep();

      engine.Close();
    }

    RecordTime("CabM checkpointWrite", overall_timer.seconds(), btime);
    Kokkos::Profiling::popRegion();
  }

  template <class DataTypes, typename MemSpace>
  void CabM<DataTypes, MemSpace>::checkpointRead(std::string path) {
    const auto btime = prebarrier();
    Kokkos::Profiling::pushRegion("CabM checkpointRead");
    Kokkos::Timer overall_timer;

    int comm_rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &comm_rank);
    if (!comm_rank) {
      fprintf(stderr, "doing adios2 stuff in cabm checkpointRead\n");

      std::string config = "../../pumi-pic/pumipic-data/checkpoint/adios2.xml";
      adios2::ADIOS adios(config, MPI_COMM_WORLD);
      adios2::IO io = adios.DeclareIO("readerIO");

      adios2::Engine engine = io.Open(path, adios2::Mode::Read);

      // single-element variables
      adios2::Variable<lid_t> var_num_elems = io.InquireVariable<lid_t>("num_elems");
      adios2::Variable<lid_t> var_num_ptcls = io.InquireVariable<lid_t>("num_ptcls");
      adios2::Variable<lid_t> var_padding_start = io.InquireVariable<lid_t>("padding_start");
      adios2::Variable<double> var_extra_padding = io.InquireVariable<double>("extra_padding");
      adios2::Variable<std::string> var_name = io.InquireVariable<std::string>("name");

      engine.BeginStep();
      engine.Get(var_num_elems, num_elems);
      engine.Get(var_num_ptcls, num_ptcls);
      engine.Get(var_padding_start, padding_start);
      engine.Get(var_extra_padding, extra_padding);
      engine.Get(var_name, name);
      engine.EndStep();

      // array-sized variables
      adios2::Variable<lid_t> var_ppe = io.InquireVariable<lid_t>("particles_per_element");
      //adios2::Variable<lid_t> var_ptcl_elems = io.InquireVariable<lid_t>("particle_elements");
      adios2::Variable<gid_t> var_gids = io.InquireVariable<gid_t>("element_gids");
      //Kokkos::View<lid_t*,host_space> particle_elements_h(Kokkos::ViewAllocateWithoutInitializing("particle_elements_h"), num_ptcls);
      Kokkos::View<lid_t*,host_space> particles_per_element_h(Kokkos::ViewAllocateWithoutInitializing("particles_per_element_h"), num_elems);
      Kokkos::View<gid_t*,host_space> element_gids_h(Kokkos::ViewAllocateWithoutInitializing("element_gids_h"), num_elems);
      
      // TODO: AoSoA variable

      // TODO: Check if types are the same before moving on

      engine.BeginStep();
      //engine.Get(var_ptcl_elems, particle_elements_h.data());
      engine.Get(var_ppe, particles_per_element_h.data());
      engine.Get(var_gids, element_gids_h.data());
      engine.EndStep();

      //kkLidView particle_elements_d(Kokkos::ViewAllocateWithoutInitializing("particle_elements_d"), num_ptcls);
      kkLidView particles_per_element_d(Kokkos::ViewAllocateWithoutInitializing("particles_per_element_d"), num_elems);
      kkGidView element_gids(Kokkos::ViewAllocateWithoutInitializing("element_gids"), num_elems);
      //hostToDevice(particle_elements_d, particle_elements_h.data());
      hostToDevice(particles_per_element_d, particles_per_element_h.data());
      hostToDevice(element_gids, element_gids_h.data());

      engine.Close();
    }

    RecordTime("CabM checkpointRead", overall_timer.seconds(), btime);
    Kokkos::Profiling::popRegion();
  }

}