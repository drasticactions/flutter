// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_IMPELLER_RENDERER_BACKEND_METAL_COMMAND_BUFFER_MTL_H_
#define FLUTTER_IMPELLER_RENDERER_BACKEND_METAL_COMMAND_BUFFER_MTL_H_

#include <Metal/Metal.h>

#include <optional>

#include "impeller/core/allocator.h"
#include "impeller/renderer/command_buffer.h"

namespace impeller {

class ContextMTL;

class CommandBufferMTL final : public CommandBuffer {
 public:
  // |CommandBuffer|
  ~CommandBufferMTL() override;

 private:
  friend class ContextMTL;

  id<MTLCommandBuffer> buffer_ = nil;
  id<MTLDevice> device_ = nil;
  // Encodes into ContextMTL's shared pass buffer at this depth; submitting
  // doesn't commit.
  std::optional<size_t> shared_pass_depth_;

  CommandBufferMTL(const std::weak_ptr<const Context>& context,
                   id<MTLDevice> device,
                   id<MTLCommandQueue> queue);

  CommandBufferMTL(const std::weak_ptr<const Context>& context,
                   id<MTLDevice> device,
                   id<MTLCommandBuffer> shared_buffer,
                   size_t shared_pass_depth);

  // |CommandBuffer|
  void SetLabel(std::string_view label) const override;

  // |CommandBuffer|
  bool IsValid() const override;

  // |CommandBuffer|
  bool OnSubmitCommands(CompletionCallback callback) override;

  // |CommandBuffer|
  SubmitResult OnSubmitCommandsWithReceipt(
      CompletionCallback callback) override;

  // |CommandBuffer|
  void OnWaitUntilCompleted() override;

  // |CommandBuffer|
  void OnWaitUntilScheduled() override;

  // |CommandBuffer|
  std::shared_ptr<RenderPass> OnCreateRenderPass(RenderTarget target) override;

  // |CommandBuffer|
  std::shared_ptr<BlitPass> OnCreateBlitPass() override;

  // |CommandBuffer|
  std::shared_ptr<ComputePass> OnCreateComputePass() override;

  SubmitResult SubmitCommandsInternal(bool create_scheduling_receipt,
                                      CompletionCallback callback);

  SubmitResult SubmitSharedPass(const ContextMTL& context,
                                bool create_scheduling_receipt,
                                CompletionCallback callback);

  CommandBufferMTL(const CommandBufferMTL&) = delete;

  CommandBufferMTL& operator=(const CommandBufferMTL&) = delete;
};

}  // namespace impeller

#endif  // FLUTTER_IMPELLER_RENDERER_BACKEND_METAL_COMMAND_BUFFER_MTL_H_
