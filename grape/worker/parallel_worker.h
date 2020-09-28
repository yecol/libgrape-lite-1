/** Copyright 2020 Alibaba Group Holding Limited.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

#ifndef GRAPE_WORKER_PARALLEL_WORKER_H_
#define GRAPE_WORKER_PARALLEL_WORKER_H_

#include <mpi.h>

#include <memory>
#include <ostream>
#include <type_traits>
#include <utility>

#include "grape/communication/communicator.h"
#include "grape/parallel/parallel_engine.h"
#include "grape/parallel/parallel_message_manager.h"
#include "grape/worker/comm_spec.h"
#include "grape/config.h"

/**
 * @brief  A Worker manages the computation cycle. ParallelWorker is a kind of
 * worker for apps derived from ParallelAppBase.
 *
 */
namespace grape {

template <typename FRAG_T, typename CONTEXT_T>
class ParallelAppBase;

template <typename APP_T>
class ParallelWorker {
  static_assert(std::is_base_of<ParallelAppBase<typename APP_T::fragment_t,
                                                typename APP_T::context_t>,
                                APP_T>::value,
                "ParallelWorker should work with ParallelApp");

 public:
  using fragment_t = typename APP_T::fragment_t;
  using context_t = typename APP_T::context_t;

  using message_manager_t = ParallelMessageManager;

  static_assert(check_app_fragment_consistency<APP_T, fragment_t>(),
                "The loaded graph is not valid for application");

  ParallelWorker(std::shared_ptr<APP_T> app, std::shared_ptr<fragment_t> graph)
      : app_(app), graph_(graph) {}

  ~ParallelWorker() = default;

  void Init(const CommSpec& comm_spec,
            const ParallelEngineSpec& pe_spec = DefaultParallelEngineSpec()) {
    // prepare for the query
    graph_->PrepareToRunApp(APP_T::message_strategy, APP_T::need_split_edges);

    comm_spec_ = comm_spec;

    messages_.Init(comm_spec_.comm());

    InitParallelEngine(app_, pe_spec);
    InitCommunicator(app_, comm_spec_.comm());
  }

  void Finalize() {}

  template <class... Args>
  void Query(Args&&... args) {
    MPI_Barrier(comm_spec_.comm());

    context_ = std::make_shared<context_t>();
    context_->set_fragment(graph_);
    context_->Init(messages_, std::forward<Args>(args)...);
    if (comm_spec_.worker_id() == kCoordinatorRank) {
      VLOG(1) << "[Coordinator]: Finished Init";
    }

    int round = 0;

    messages_.Start();

    messages_.StartARound();

    app_->PEval(*graph_, *context_, messages_);

    messages_.FinishARound();

    if (comm_spec_.worker_id() == kCoordinatorRank) {
      VLOG(1) << "[Coordinator]: Finished PEval";
    }

    int step = 1;

    while (!messages_.ToTerminate()) {
      round++;
      messages_.StartARound();

      app_->IncEval(*graph_, *context_, messages_);

      messages_.FinishARound();

      if (comm_spec_.worker_id() == kCoordinatorRank) {
        VLOG(1) << "[Coordinator]: Finished IncEval - " << step;
      }
      ++step;
    }
    MPI_Barrier(comm_spec_.comm());
    messages_.Finalize();
  }

  std::shared_ptr<context_t> GetContext() { return context_; }

  void Output(std::ostream& os) { context_->Output(os); }

 private:
  std::shared_ptr<APP_T> app_;
  std::shared_ptr<fragment_t> graph_;
  std::shared_ptr<context_t> context_;
  message_manager_t messages_;

  CommSpec comm_spec_;
};

}  // namespace grape

#endif  // GRAPE_WORKER_PARALLEL_WORKER_H_
