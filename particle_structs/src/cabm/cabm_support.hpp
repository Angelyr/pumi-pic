#pragma once

#include <Cabana_Core.hpp>
#include <psMemberType.h>
namespace pumipic {
  /* Class which appends a type T to a pp::MemberType and provides it as a
       cabana::MemberType
     Usage: typename AppendMT<Type, MemberTypes>::type
  */
  template <typename T, typename... Types> struct AppendMT;

  template <typename PS, typename... Types> struct CopyMTVsToAoSoA;

//Append type to the end
  template <typename T, typename... Types>
  struct AppendMT<T, particle_structs::MemberTypes<Types...> > {
    static constexpr int size = 1 + Cabana::MemberTypes<Types...>::size;
    using type = Cabana::MemberTypes<Types..., T>; //Put T before Types... to put at beginning
  };

  template <typename DataTypes> using CM_DTInt = typename AppendMT<int,DataTypes>::type;

  //Forward declaration of CabM
  template <class DataTypes, typename MemSpace> class CabM;


  template <typename ViewT, std::size_t Rank>
  using CheckRank = typename std::enable_if<(ViewT::rank == Rank), void>::type;

  /*
    Copy view to Soa functions
   */
  //Rank 0
  template <std::size_t M, typename SoA_t, typename View_t>
  PP_INLINE CheckRank<View_t, 1> copyViewToSoa(SoA_t dst, lid_t dstind,
                                               View_t src, lid_t srcind) {
    Cabana::get<M>(dst, dstind) = src(srcind);
  }
  //Rank 1
  template <std::size_t M, typename SoA_t, typename View_t>
  PP_INLINE CheckRank<View_t, 2> copyViewToSoa(SoA_t dst, lid_t dstind,
                                               View_t src, lid_t srcind) {
    for (lid_t i = 0; i < src.extent(1); ++i)
      Cabana::get<M>(dst, dstind, i) = src(srcind, i);
  }
  //Rank 2
  template <std::size_t M, typename SoA_t, typename View_t>
  PP_INLINE CheckRank<View_t, 3> copyViewToSoa(SoA_t dst, lid_t dstind,
                                               View_t src, lid_t srcind) {
    for (lid_t i = 0; i < src.extent(1); ++i)
      for (lid_t j = 0; j < src.extent(2); ++j)
        Cabana::get<M>(dst, dstind, i, j) = src(srcind, i, j);
  }
  //Rank 3
  template <std::size_t M, typename SoA_t, typename View_t>
  PP_INLINE CheckRank<View_t, 4> copyViewToSoa(SoA_t dst, lid_t dstind,
                                               View_t src, lid_t srcind) {
    for (lid_t i = 0; i < src.extent(1); ++i)
      for (lid_t j = 0; j < src.extent(2); ++j)
        for (lid_t k = 0; k < src.extent(3); ++k)
          Cabana::get<M>(dst, dstind, i, j, k) = src(srcind, i, j, k);
  }

  //Per type copy from MTVs to AoSoA
  template <typename Device, std::size_t M, typename CMDT, typename ViewT,
            typename... Types>
  struct CopyMTVsToAoSoAImpl;

  template <typename Device, std::size_t M, typename CMDT, typename ViewT>
  struct CopyMTVsToAoSoAImpl<Device, M, CMDT, ViewT> {
    typedef Cabana::AoSoA<CMDT, Device> Aosoa;
    CopyMTVsToAoSoAImpl(Aosoa, MemberTypeViewsConst, ViewT, ViewT) {}
  };

  template <typename Device, std::size_t M, typename CMDT, typename ViewT,
            typename T, typename... Types>
  struct CopyMTVsToAoSoAImpl<Device, M, CMDT, ViewT, T, Types...> {
    typedef Cabana::AoSoA<CMDT, Device> Aosoa;
    CopyMTVsToAoSoAImpl(Aosoa dst, MemberTypeViewsConst srcs, ViewT soa_indices,
                        ViewT soa_ptcl_indices) {
      enclose(dst, srcs, soa_indices, soa_ptcl_indices);
      CopyMTVsToAoSoAImpl<Device, M+1, CMDT, ViewT, Types...>(dst, srcs+1,
                                                              soa_indices,
                                                              soa_ptcl_indices);
    }
    void enclose(Aosoa dst, MemberTypeViewsConst srcs, ViewT soa_indices,
                 ViewT soa_ptcl_indices) {
      MemberTypeView<T, Device> src =
        *static_cast<MemberTypeView<T, Device> const*>(srcs[0]);
      auto MTVToAoSoA = KOKKOS_LAMBDA(const int index) {
        const lid_t soa = soa_indices(index);
        const lid_t pid = soa_ptcl_indices(index);
        copyViewToSoa<M>(dst.access(soa), pid, src, index);
      };
      Kokkos::parallel_for(soa_indices.size(), MTVToAoSoA, "copyMTVtoAoSoA");
    }
  };

  //High level copy from MTVs to AoSoA
  template <typename Device, typename... Types>
  struct CopyMTVsToAoSoA<Device, MemberTypes<Types...>> {

    typedef CabM<MemberTypes<Types...>, Device> PS;
    typedef Cabana::AoSoA<CM_DTInt<MemberTypes<Types...>>, Device> Aosoa;
    typedef CM_DTInt<MemberTypes<Types...>> CM_DT;

    CopyMTVsToAoSoA(Aosoa dst, MemberTypeViewsConst src,
                    typename PS::kkLidView soa_indices,
                    typename PS::kkLidView soa_ptcl_indices) {
      if (src != NULL)
        CopyMTVsToAoSoAImpl<Device, 0, CM_DT, typename PS::kkLidView,
                            Types...>(dst, src, soa_indices, soa_ptcl_indices);
    }
  };
}