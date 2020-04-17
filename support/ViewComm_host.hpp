/************** Host Communication functions **************/
template <typename View> using ViewType = typename View::traits::data_type;
template <typename View> using ViewSpace = typename View::traits::memory_space;
template <typename Space> using IsHost =
  // typename std::enable_if<std::is_same<typename Space::memory_space, Kokkos::HostSpace>::value, int>::type;
  typename std::enable_if<Kokkos::SpaceAccessibility<typename Space::memory_space,
                                                     Kokkos::HostSpace>::accessible, int>::type;
//Send
template <typename ViewT>
IsHost<ViewSpace<ViewT> > PS_Comm_Send(ViewT view, int offset, int size,
                                       int dest, int tag, MPI_Comm comm) {
  int size_per_entry = BT<ViewType<ViewT> >::size;
  return MPI_Send(view.data() + offset, size*size_per_entry,
                  MpiType<BT<ViewType<ViewT> > >::mpitype(), dest, tag, comm);
}
//Recv
template <typename ViewT>
IsHost<ViewSpace<ViewT> > PS_Comm_Recv(ViewT view, int offset, int size,
                                       int sender, int tag, MPI_Comm comm) {
  int size_per_entry = BT<ViewType<ViewT> >::size;
  return MPI_Recv(view.data() + offset, size*size_per_entry, MpiType<BT<ViewType<ViewT> > >::mpitype(),
                  sender, tag, comm, MPI_STATUS_IGNORE);
}
//Isend
template <typename ViewT>
IsHost<ViewSpace<ViewT> > PS_Comm_Isend(ViewT view, int offset, int size,
                                        int dest, int tag, MPI_Comm comm, MPI_Request* req) {
  int size_per_entry = BaseType<ViewType<ViewT> >::size;
  return MPI_Isend(view.data() + offset, size*size_per_entry, MpiType<BT<ViewType<ViewT> > >::mpitype(),
                   dest, tag, comm, req);
}
//Irecv
template <typename ViewT>
IsHost<ViewSpace<ViewT> > PS_Comm_Irecv(ViewT view, int offset, int size,
                                        int sender, int tag, MPI_Comm comm, MPI_Request* req) {
  int size_per_entry = BaseType<ViewType<ViewT> >::size;
  return MPI_Irecv(view.data() + offset, size*size_per_entry,
                   MpiType<BT<ViewType<ViewT> > >::mpitype(),
                   sender, tag, comm, req);
}

//Wait
template <typename Space>
IsHost<Space> PS_Comm_Wait(MPI_Request* req, MPI_Status* stat) {
  return MPI_Wait(req, stat);
}

//Waitall
template <typename Space>
IsHost<Space> PS_Comm_Waitall(int num_reqs, MPI_Request* reqs, MPI_Status* stats) {
  return MPI_Waitall(num_reqs, reqs, stats);
}
//Alltoall
template <typename ViewT>
IsHost<ViewSpace<ViewT> > PS_Comm_Alltoall(ViewT send, int send_size,
                                           ViewT recv, int recv_size,
                                           MPI_Comm comm) {
  return MPI_Alltoall(send.data(), send_size, MpiType<BT<ViewType<ViewT> > >::mpitype(),
                      recv.data(), recv_size, MpiType<BT<ViewType<ViewT> > >::mpitype(), comm);
}
