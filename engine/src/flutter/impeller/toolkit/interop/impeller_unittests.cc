// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/fml/mapping.h"
#include "flutter/fml/native_library.h"
#include "flutter/fml/string_conversion.h"
#include "flutter/testing/testing.h"
#include "impeller/base/allocation.h"
#include "impeller/base/validation.h"
#include "impeller/renderer/backend/gles/context_gles.h"
#include "impeller/toolkit/interop/context.h"
#include "impeller/toolkit/interop/dl.h"
#include "impeller/toolkit/interop/dl_builder.h"
#include "impeller/toolkit/interop/formats.h"
#include "impeller/toolkit/interop/impeller.h"
#include "impeller/toolkit/interop/impeller.hpp"
#include "impeller/toolkit/interop/paint.h"
#include "impeller/toolkit/interop/paragraph.h"
#include "impeller/toolkit/interop/paragraph_builder.h"
#include "impeller/toolkit/interop/paragraph_style.h"
#include "impeller/toolkit/interop/playground_test.h"
#include "impeller/toolkit/interop/surface.h"
#include "impeller/toolkit/interop/texture.h"
#include "impeller/toolkit/interop/typography_context.h"

namespace impeller::interop::testing {

using InteropPlaygroundTest = PlaygroundTest;
INSTANTIATE_PLAYGROUND_SUITE(InteropPlaygroundTest);

// Just ensures that context can be subclassed.
class ContextSub : public hpp::Context {};

TEST_P(InteropPlaygroundTest, CanCreateContext) {
  auto context = CreateContext();
  ASSERT_TRUE(context);
}

TEST_P(InteropPlaygroundTest, CanCreateDisplayListBuilder) {
  hpp::DisplayListBuilder builder;
  ASSERT_TRUE(builder);
  ASSERT_TRUE(ToImpellerType(builder.GetTransform()).IsIdentity());
  ASSERT_EQ(builder.GetSaveCount(), 1u);
  builder.Save();
  ASSERT_EQ(builder.GetSaveCount(), 2u);
  builder.Restore();
  ASSERT_EQ(builder.GetSaveCount(), 1u);
}

TEST_P(InteropPlaygroundTest, CanCreateSurface) {
  if (GetBackend() != PlaygroundBackend::kOpenGLES &&
      GetBackend() != PlaygroundBackend::kOpenGLESSDF) {
    GTEST_SKIP()
        << "This test checks wrapping FBOs which is an OpenGL ES only call.";
    return;
  }
  auto context = CreateContext();
  ASSERT_TRUE(context);
  const auto window_size = GetWindowSize();
  ImpellerISize size = {window_size.width, window_size.height};
  auto surface = Adopt<Surface>(ImpellerSurfaceCreateWrappedFBONew(
      context.GetC(),                                     //
      0u,                                                 //
      ImpellerPixelFormat::kImpellerPixelFormatRGBA8888,  //
      &size)                                              //
  );
  ASSERT_TRUE(surface);
}

TEST_P(InteropPlaygroundTest, CanDrawRect) {
  auto builder =
      Adopt<DisplayListBuilder>(ImpellerDisplayListBuilderNew(nullptr));
  auto paint = Adopt<Paint>(ImpellerPaintNew());
  ImpellerColor color = {0.0, 0.0, 1.0, 1.0};
  ImpellerPaintSetColor(paint.GetC(), &color);
  ImpellerRect rect = {10, 20, 100, 200};
  ImpellerDisplayListBuilderDrawRect(builder.GetC(), &rect, paint.GetC());
  color = {1.0, 0.0, 0.0, 1.0};
  ImpellerPaintSetColor(paint.GetC(), &color);
  ImpellerDisplayListBuilderTranslate(builder.GetC(), 110, 210);
  ImpellerMatrix scale_transform = {
      // clang-format off
      2.0, 0.0, 0.0, 0.0, //
      0.0, 2.0, 0.0, 0.0, //
      0.0, 0.0, 1.0, 0.0, //
      0.0, 0.0, 0.0, 1.0, //
      // clang-format on
  };
  ImpellerDisplayListBuilderTransform(builder.GetC(), &scale_transform);
  ImpellerDisplayListBuilderDrawRect(builder.GetC(), &rect, paint.GetC());
  auto dl = Adopt<DisplayList>(
      ImpellerDisplayListBuilderCreateDisplayListNew(builder.GetC()));
  ASSERT_TRUE(dl);
  ASSERT_TRUE(
      OpenPlaygroundHere([&](const auto& context, const auto& surface) -> bool {
        ImpellerSurfaceDrawDisplayList(surface.GetC(), dl.GetC());
        return true;
      }));
}

TEST_P(InteropPlaygroundTest, CanDrawImage) {
  auto compressed = LoadFixtureImageCompressed(
      flutter::testing::OpenFixtureAsMapping("boston.jpg"));
  ASSERT_NE(compressed, nullptr);
  auto decompressed = std::make_shared<impeller::DecompressedImage>(
      compressed->Decode().ConvertToRGBA());
  ASSERT_TRUE(decompressed->IsValid());
  auto mapping = std::make_unique<hpp::Mapping>(
      decompressed->GetAllocation()->GetMapping(),
      decompressed->GetAllocation()->GetSize(), [decompressed]() {
        // Mapping will be dropped on the floor.
      });

  auto context = GetHPPContext();
  ImpellerTextureDescriptor desc = {};
  desc.pixel_format = ImpellerPixelFormat::kImpellerPixelFormatRGBA8888;
  desc.size = {decompressed->GetSize().width, decompressed->GetSize().height};
  desc.mip_count = 1u;
  auto texture = hpp::Texture::WithContents(context, desc, std::move(mapping));
  ASSERT_TRUE(texture);

  auto dl = hpp::DisplayListBuilder{}
                .DrawTexture(texture, {100, 100},
                             kImpellerTextureSamplingLinear, hpp::Paint{})
                .Build();

  ASSERT_TRUE(
      OpenPlaygroundHere([&](const auto& context, const auto& surface) -> bool {
        hpp::Surface window(surface.GetC());
        window.Draw(dl);
        return true;
      }));
}

TEST_P(InteropPlaygroundTest, CanCreateOpenGLImage) {
  auto context = GetInteropContext();

  auto impeller_context = context->GetContext();

  if (impeller_context->GetBackendType() !=
      impeller::Context::BackendType::kOpenGLES) {
    GTEST_SKIP() << "This test works with OpenGL handles is only suitable for "
                    "that backend.";
    return;
  }

  const auto& gl_context = ContextGLES::Cast(*impeller_context);
  const auto& gl = gl_context.GetReactor()->GetProcTable();

  constexpr ISize external_texture_size = {200, 300};

  Allocation texture_data;
  ASSERT_TRUE(
      texture_data.Truncate(Bytes{external_texture_size.Area() * 4u}, false));

  const auto kClearColor = Color::Fuchsia().ToR8G8B8A8();

  for (size_t i = 0; i < external_texture_size.Area() * 4u; i += 4u) {
    memcpy(texture_data.GetBuffer() + i, kClearColor.data(), 4);
  }

  GLuint external_texture = GL_NONE;
  gl.GenTextures(1u, &external_texture);
  ASSERT_NE(external_texture, 0u);
  gl.BindTexture(GL_TEXTURE_2D, external_texture);
  gl.TexImage2D(GL_TEXTURE_2D,                 //
                0,                             //
                GL_RGBA,                       //
                external_texture_size.width,   //
                external_texture_size.height,  //
                0,                             //
                GL_RGBA,                       //
                GL_UNSIGNED_BYTE,              //
                texture_data.GetBuffer()       //
  );

  ImpellerTextureDescriptor desc = {};
  desc.pixel_format = ImpellerPixelFormat::kImpellerPixelFormatRGBA8888;
  desc.size = {external_texture_size.width, external_texture_size.height};
  desc.mip_count = 1u;
  auto texture = Adopt<Texture>(ImpellerTextureCreateWithOpenGLTextureHandleNew(
      context.GetC(),   //
      &desc,            //
      external_texture  //
      ));
  ASSERT_TRUE(texture);

  ASSERT_EQ(ImpellerTextureGetOpenGLHandle(texture.GetC()), external_texture);

  auto builder =
      Adopt<DisplayListBuilder>(ImpellerDisplayListBuilderNew(nullptr));
  ImpellerPoint point = {100, 100};
  ImpellerDisplayListBuilderDrawTexture(builder.GetC(), texture.GetC(), &point,
                                        kImpellerTextureSamplingLinear,
                                        nullptr);
  auto dl = Adopt<DisplayList>(
      ImpellerDisplayListBuilderCreateDisplayListNew(builder.GetC()));
  ASSERT_TRUE(
      OpenPlaygroundHere([&](const auto& context, const auto& surface) -> bool {
        ImpellerSurfaceDrawDisplayList(surface.GetC(), dl.GetC());
        return true;
      }));
}

TEST_P(InteropPlaygroundTest, ClearsOpenGLStancilStateAfterTransition) {
  auto context = GetInteropContext();
  auto impeller_context = context->GetContext();
  if (impeller_context->GetBackendType() !=
      impeller::Context::BackendType::kOpenGLES) {
    GTEST_SKIP() << "This test works with OpenGL handles is only suitable for "
                    "that backend.";
    return;
  }
  const auto& gl_context = ContextGLES::Cast(*impeller_context);
  const auto& gl = gl_context.GetReactor()->GetProcTable();
  auto builder =
      Adopt<DisplayListBuilder>(ImpellerDisplayListBuilderNew(nullptr));
  auto paint = Adopt<Paint>(ImpellerPaintNew());
  ImpellerColor color = {0.0, 0.0, 1.0, 1.0};
  ImpellerPaintSetColor(paint.GetC(), &color);
  ImpellerRect rect = {10, 20, 100, 200};
  ImpellerDisplayListBuilderDrawRect(builder.GetC(), &rect, paint.GetC());
  color = {1.0, 0.0, 0.0, 1.0};
  ImpellerPaintSetColor(paint.GetC(), &color);
  ImpellerDisplayListBuilderTranslate(builder.GetC(), 110, 210);
  ImpellerDisplayListBuilderClipRect(builder.GetC(), &rect,
                                     kImpellerClipOperationDifference);
  ImpellerDisplayListBuilderDrawRect(builder.GetC(), &rect, paint.GetC());
  auto dl = Adopt<DisplayList>(
      ImpellerDisplayListBuilderCreateDisplayListNew(builder.GetC()));
  ASSERT_TRUE(dl);
  ASSERT_TRUE(
      OpenPlaygroundHere([&](const auto& context, const auto& surface) -> bool {
        ImpellerSurfaceDrawDisplayList(surface.GetC(), dl.GetC());
        // OpenGL state is reset even though the operations above enable a
        // stencil check.
        GLboolean stencil_enabled = true;
        gl.GetBooleanv(GL_STENCIL_TEST, &stencil_enabled);
        return stencil_enabled == GL_FALSE;
      }));
}

TEST_P(InteropPlaygroundTest, CanCreateParagraphs) {
  // Create a typography context.
  hpp::TypographyContext type_context;
  ASSERT_TRUE(type_context);

  // Create a builder.
  hpp::ParagraphBuilder builder(type_context);
  ASSERT_TRUE(builder);

  // Create a paragraph style with the font size and foreground and background
  // colors.
  hpp::ParagraphStyle style;
  ASSERT_TRUE(style);
  style.SetFontSize(150.0f);
  style.SetHeight(2.0f);

  {
    hpp::Paint paint;
    ASSERT_TRUE(paint);
    paint.SetColor({1.0, 0.0, 0.0, 1.0});
    style.SetForeground(paint);
  }

  {
    hpp::Paint paint;
    paint.SetColor({1.0, 1.0, 1.0, 1.0});
    style.SetBackground(paint);
  }

  // Push the style onto the style stack.
  builder.PushStyle(style);
  std::string text = "the ⚡️ quick ⚡️ brown 🦊 fox jumps over the lazy dog 🐶.";

  // Add the paragraph text data.
  builder.AddText(text);

  // Layout and build the paragraph.
  auto paragraph = builder.Build(1200.0f);
  ASSERT_TRUE(paragraph);

  // Create a display list with just the paragraph drawn into it.
  hpp::DisplayListBuilder dl_builder;
  dl_builder.DrawParagraph(paragraph, {20, 20});

  // Build the display list.
  auto dl = dl_builder.Build();

  ASSERT_TRUE(
      OpenPlaygroundHere([&](const auto& context, const auto& surface) -> bool {
        hpp::Surface window(surface.GetC());
        window.Draw(dl);
        return true;
      }));
}

TEST_P(InteropPlaygroundTest, CanCreateDecorations) {
  hpp::TypographyContext context;
  auto para =
      hpp::ParagraphBuilder(context)
          .PushStyle(
              hpp::ParagraphStyle{}
                  .SetForeground(hpp::Paint{}.SetColor({1.0, 0.0, 0.0, 1.0}))
                  .SetFontSize(150.0f)
                  .SetTextDecoration(ImpellerTextDecoration{
                      .types = kImpellerTextDecorationTypeLineThrough |
                               kImpellerTextDecorationTypeUnderline,
                      .color = ImpellerColor{0.0, 1.0, 0.0, 0.75},
                      .style = kImpellerTextDecorationStyleWavy,
                      .thickness_multiplier = 1.5,
                  }))
          .AddText(std::string{"Holy text decorations Batman!"})
          .Build(900);
  auto dl = hpp::DisplayListBuilder{}.DrawParagraph(para, {100, 100}).Build();
  ASSERT_TRUE(
      OpenPlaygroundHere([&](const auto& context, const auto& surface) -> bool {
        hpp::Surface window(surface.GetC());
        window.Draw(dl);
        return true;
      }));
}

TEST_P(InteropPlaygroundTest, CanCreateShapes) {
  hpp::DisplayListBuilder builder;

  hpp::Paint red_paint;
  red_paint.SetColor({1.0, 0.0, 0.0, 1.0});
  red_paint.SetStrokeWidth(10.0);

  builder.Translate(10, 10);
  builder.DrawRect({0, 0, 100, 100}, red_paint);
  builder.Translate(100, 100);
  builder.DrawOval({0, 0, 100, 100}, red_paint);
  builder.Translate(100, 100);
  builder.DrawLine({0, 0}, {100, 100}, red_paint);

  builder.Translate(100, 100);
  ImpellerRoundingRadii radii = {};
  radii.top_left = {10, 10};
  radii.bottom_right = {10, 10};
  builder.DrawRoundedRect({0, 0, 100, 100}, radii, red_paint);

  builder.Translate(100, 100);
  builder.DrawPath(hpp::PathBuilder{}.AddOval({0, 0, 100, 100}).Build(),
                   red_paint);

  auto dl = builder.Build();
  ASSERT_TRUE(
      OpenPlaygroundHere([&](const auto& context, const auto& surface) -> bool {
        hpp::Surface window(surface.GetC());
        window.Draw(dl);
        return true;
      }));
}

TEST_P(InteropPlaygroundTest, CanCreateParagraphsWithCustomFont) {
  // Create a typography context.
  auto type_context = Adopt<TypographyContext>(ImpellerTypographyContextNew());
  ASSERT_TRUE(type_context);

  // Open the custom font file.
  std::unique_ptr<fml::Mapping> font_data =
      flutter::testing::OpenFixtureAsMapping("wtf.otf");
  ASSERT_NE(font_data, nullptr);
  ASSERT_GT(font_data->GetSize(), 0u);
  ImpellerMapping font_data_mapping = {
      .data = font_data->GetMapping(),
      .length = font_data->GetSize(),
      .on_release = [](auto ctx) {
        delete reinterpret_cast<fml::Mapping*>(ctx);
      }};
  auto registered =
      ImpellerTypographyContextRegisterFont(type_context.GetC(),  //
                                            &font_data_mapping,   //
                                            font_data.release(),  //
                                            nullptr               //
      );
  ASSERT_TRUE(registered);

  // Create a builder.
  auto builder =
      Adopt<ParagraphBuilder>(ImpellerParagraphBuilderNew(type_context.GetC()));
  ASSERT_TRUE(builder);

  // Create a paragraph style with the font size and foreground and background
  // colors.
  auto style = Adopt<ParagraphStyle>(ImpellerParagraphStyleNew());
  ASSERT_TRUE(style);
  ImpellerParagraphStyleSetFontSize(style.GetC(), 150.0f);
  ImpellerParagraphStyleSetFontFamily(style.GetC(), "WhatTheFlutter");

  {
    auto paint = Adopt<Paint>(ImpellerPaintNew());
    ASSERT_TRUE(paint);
    ImpellerColor color = {0.0, 1.0, 1.0, 1.0};
    ImpellerPaintSetColor(paint.GetC(), &color);
    ImpellerParagraphStyleSetForeground(style.GetC(), paint.GetC());
  }

  // Push the style onto the style stack.
  ImpellerParagraphBuilderPushStyle(builder.GetC(), style.GetC());
  std::string text = "0F0F0F0";

  // Add the paragraph text data.
  ImpellerParagraphBuilderAddText(builder.GetC(),
                                  reinterpret_cast<const uint8_t*>(text.data()),
                                  text.size());

  // Layout and build the paragraph.
  auto paragraph = Adopt<Paragraph>(
      ImpellerParagraphBuilderBuildParagraphNew(builder.GetC(), 1200.0f));
  ASSERT_TRUE(paragraph);

  // Create a display list with just the paragraph drawn into it.
  auto dl_builder =
      Adopt<DisplayListBuilder>(ImpellerDisplayListBuilderNew(nullptr));
  ImpellerPoint point = {20, 20};
  ImpellerDisplayListBuilderDrawParagraph(dl_builder.GetC(), paragraph.GetC(),
                                          &point);
  auto dl = Adopt<DisplayList>(
      ImpellerDisplayListBuilderCreateDisplayListNew(dl_builder.GetC()));

  ASSERT_TRUE(
      OpenPlaygroundHere([&](const auto& context, const auto& surface) -> bool {
        ImpellerSurfaceDrawDisplayList(surface.GetC(), dl.GetC());
        return true;
      }));
}  // namespace impeller::interop::testing

static void DrawTextFrame(const hpp::TypographyContext& tc,
                          hpp::DisplayListBuilder& builder,
                          hpp::ParagraphStyle& p_style,
                          const hpp::Paint& bg,
                          ImpellerColor color,
                          ImpellerTextAlignment align,
                          float x_offset) {
  const char text[] =
      "Lorem ipsum dolor sit amet, consectetur adipiscing elit.";

  hpp::Paint fg;

  // Draw a box.
  fg.SetColor(color);
  fg.SetDrawStyle(kImpellerDrawStyleStroke);
  ImpellerRect box_rect = {10 + x_offset, 10, 200, 200};
  builder.DrawRect(box_rect, fg);

  // Draw text.
  fg.SetDrawStyle(kImpellerDrawStyleFill);
  p_style.SetForeground(fg);
  p_style.SetBackground(bg);
  p_style.SetTextAlignment(align);

  hpp::ParagraphBuilder p_builder(tc);
  p_builder.PushStyle(p_style);
  p_builder.AddText(reinterpret_cast<const uint8_t*>(text), sizeof(text));

  auto left_p = p_builder.Build(box_rect.width - 20.0);
  ImpellerPoint pt = {20.0f + x_offset, 20.0f};
  float w = left_p.GetMaxWidth();
  float h = left_p.GetHeight();
  builder.DrawParagraph(left_p, pt);
  fg.SetDrawStyle(kImpellerDrawStyleStroke);

  // Draw an inner box around the paragraph layout.
  ImpellerRect inner_box_rect = {pt.x, pt.y, w, h};
  builder.DrawRect(inner_box_rect, fg);
}

TEST_P(InteropPlaygroundTest, CanRenderTextAlignments) {
  hpp::TypographyContext tc;

  hpp::DisplayListBuilder builder;
  hpp::Paint bg;
  hpp::ParagraphStyle p_style;
  p_style.SetFontFamily("Roboto");
  p_style.SetFontSize(24.0);
  p_style.SetFontWeight(kImpellerFontWeight400);

  // Clear the background to a white color.
  ImpellerColor clear_color = {1.0, 1.0, 1.0, 1.0};
  bg.SetColor(clear_color);
  builder.DrawPaint(bg);

  // Draw red, left-aligned text.
  ImpellerColor red = {1.0, 0.0, 0.0, 1.0};
  DrawTextFrame(tc, builder, p_style, bg, red, kImpellerTextAlignmentLeft, 0.0);

  // Draw green, centered text.
  ImpellerColor green = {0.0, 1.0, 0.0, 1.0};
  DrawTextFrame(tc, builder, p_style, bg, green, kImpellerTextAlignmentCenter,
                220.0);

  // Draw blue, right-aligned text.
  ImpellerColor blue = {0.0, 0.0, 1.0, 1.0};
  DrawTextFrame(tc, builder, p_style, bg, blue, kImpellerTextAlignmentRight,
                440.0);

  auto dl = builder.Build();

  ASSERT_TRUE(
      OpenPlaygroundHere([&](const auto& context, const auto& surface) -> bool {
        hpp::Surface window(surface.GetC());
        window.Draw(dl);
        return true;
      }));
}

TEST_P(InteropPlaygroundTest, CanRenderShadows) {
  hpp::DisplayListBuilder builder;
  {
    builder.DrawRect(ImpellerRect{0, 0, 400, 400},
                     hpp::Paint{}.SetColor(ImpellerColor{
                         0.0, 1.0, 0.0, 1.0, kImpellerColorSpaceSRGB}));
  }
  ImpellerRect box = {100, 100, 100, 100};
  {
    hpp::PathBuilder path_builder;
    path_builder.AddRect(box);
    ImpellerColor shadow_color = {0.0, 0.0, 0.0, 1.0, kImpellerColorSpaceSRGB};
    builder.DrawShadow(path_builder.Build(), shadow_color, 4.0f, false, 1.0f);
  }
  {
    hpp::Paint red_paint;
    red_paint.SetColor(
        ImpellerColor{1.0, 0.0, 0.0, 1.0, kImpellerColorSpaceSRGB});
    builder.DrawRect(box, red_paint);
  }
  auto dl = builder.Build();
  ASSERT_TRUE(
      OpenPlaygroundHere([&](const auto& context, const auto& surface) -> bool {
        hpp::Surface window(surface.GetC());
        window.Draw(dl);
        return true;
      }));
}

TEST_P(InteropPlaygroundTest, CanMeasureText) {
  hpp::TypographyContext type_context;
  hpp::ParagraphBuilder paragraph_builder(type_context);
  hpp::ParagraphStyle paragraph_style;
  paragraph_style.SetFontSize(50);
  paragraph_builder.PushStyle(paragraph_style);
  const std::string text =
      "🏁 Can 👨‍👨‍👦‍👦 Measure 🔍 Text\nAnd this is line "
      "two.\nWhoa! Three lines. How high does this go?\r\nI stopped counting.";
  const auto u16text = fml::Utf8ToUtf16(text);
  ASSERT_NE(text.size(), u16text.size());
  paragraph_builder.AddText(reinterpret_cast<const uint8_t*>(text.data()),
                            text.size());
  hpp::DisplayListBuilder builder;
  // Don't rely on implicit line breaks in this test to make it less brittle to
  // different fonts being picked.
  hpp::Paragraph paragraph = paragraph_builder.Build(FLT_MAX);
  const auto line_count = paragraph.GetLineCount();
  ASSERT_EQ(line_count, 4u);

  // Line Metrics.
  {
    auto metrics = paragraph.GetLineMetrics();
    ASSERT_GT(metrics.GetAscent(0), 0.0);
    ASSERT_GT(metrics.GetUnscaledAscent(0), 0.0);
    ASSERT_GT(metrics.GetDescent(0), 0.0);
    ASSERT_GT(metrics.GetBaseline(0), 0.0);
    ASSERT_TRUE(metrics.IsHardbreak(0));
    ASSERT_DOUBLE_EQ(metrics.GetLeft(0), 0.0);
    ASSERT_EQ(metrics.GetCodeUnitStartIndex(0), 0u);
    ASSERT_EQ(metrics.GetCodeUnitEndIndexIncludingNewline(0),
              metrics.GetCodeUnitEndIndex(0) + 1u);
    ASSERT_GT(metrics.GetCodeUnitStartIndex(1), 0u);
    // Last line should cover the entire range.
    ASSERT_EQ(metrics.GetCodeUnitEndIndex(3), u16text.size());
  }

  // Glyph info by code point.
  {
    auto glyph = paragraph.GlyphInfoAtCodeUnitIndex(0u);
    ASSERT_TRUE(glyph);
    ASSERT_EQ(glyph.GetGraphemeClusterCodeUnitRangeBegin(), 0u);
    ASSERT_EQ(glyph.GetGraphemeClusterCodeUnitRangeEnd(),
              fml::Utf8ToUtf16("🏁").size());
    auto bounds = glyph.GetGraphemeClusterBounds();
    ASSERT_GT(bounds.width, 0.0);
    ASSERT_GT(bounds.height, 0.0);
    ASSERT_FALSE(glyph.IsEllipsis());
    ASSERT_EQ(glyph.GetTextDirection(), kImpellerTextDirectionLTR);

    ImpellerRect bounds2 = {};
    ImpellerGlyphInfoGetGraphemeClusterBounds(glyph.Get(), &bounds2);
    ASSERT_EQ(bounds.width, bounds2.width);
    ASSERT_EQ(bounds.height, bounds2.height);
  }

  // Glyph info by coordinates.
  {
    auto glyph = paragraph.GlyphInfoAtParagraphCoordinates(0.0, 0.0);
    ASSERT_TRUE(glyph);
    ASSERT_EQ(glyph.GetGraphemeClusterCodeUnitRangeEnd(),
              fml::Utf8ToUtf16("🏁").size());
  }

  // Glyph Figure out word boundaries.
  {
    auto glyph = paragraph.GlyphInfoAtCodeUnitIndex(0u);
    ASSERT_TRUE(glyph);
    auto range =
        paragraph.GetWordBoundary(glyph.GetGraphemeClusterCodeUnitRangeEnd());
    ASSERT_GT(range.end, 0u);
    ImpellerRange range2 = {};
    ImpellerParagraphGetWordBoundary(
        paragraph.Get(), glyph.GetGraphemeClusterCodeUnitRangeEnd(), &range2);
    ASSERT_EQ(range.start, range2.start);
    ASSERT_EQ(range.end, range2.end);
  }

  builder.DrawParagraph(paragraph, ImpellerPoint{100, 100});
  auto dl = builder.Build();
  ASSERT_TRUE(
      OpenPlaygroundHere([&](const auto& context, const auto& surface) -> bool {
        hpp::Surface window(surface.GetC());
        window.Draw(dl);
        return true;
      }));
}

TEST_P(InteropPlaygroundTest, CanGetPathBounds) {
  const auto path =
      hpp::PathBuilder{}.MoveTo({100, 100}).LineTo({200, 200}).Build();
  const auto bounds = path.GetBounds();
  ASSERT_EQ(bounds.x, 100);
  ASSERT_EQ(bounds.y, 100);
  ASSERT_EQ(bounds.width, 100);
  ASSERT_EQ(bounds.height, 100);
}

TEST_P(InteropPlaygroundTest, CanGetPathTightBounds) {
  const auto path = hpp::PathBuilder{}
                        .MoveTo({0, 0})
                        .QuadraticCurveTo({50, 100}, {100, 0})
                        .Build();
  const auto bounds = path.GetBounds();
  const auto tight = path.GetTightBounds();
  ASSERT_FLOAT_EQ(bounds.height, 100);
  ASSERT_FLOAT_EQ(tight.height, 50);
  ASSERT_FLOAT_EQ(tight.width, 100);
}

TEST_P(InteropPlaygroundTest, CanCheckPathContainsPoint) {
  // Two nested squares: a hole with even-odd, filled with non-zero.
  auto builder = hpp::PathBuilder{};
  builder.AddRect({0, 0, 100, 100}).AddRect({25, 25, 50, 50});
  const auto odd = builder.BuildCopy(kImpellerFillTypeOdd);
  const auto non_zero = builder.Build(kImpellerFillTypeNonZero);
  ASSERT_TRUE(odd.Contains({10, 10}));
  ASSERT_FALSE(odd.Contains({50, 50}));
  ASSERT_TRUE(non_zero.Contains({50, 50}));
  ASSERT_FALSE(non_zero.Contains({150, 50}));
  ASSERT_EQ(odd.GetFillType(), kImpellerFillTypeOdd);
  ASSERT_EQ(non_zero.GetFillType(), kImpellerFillTypeNonZero);
  const auto changed = odd.WithFillType(kImpellerFillTypeNonZero);
  ASSERT_EQ(changed.GetFillType(), kImpellerFillTypeNonZero);
  ASSERT_TRUE(changed.Contains({50, 50}));
}

TEST_P(InteropPlaygroundTest, CanCheckPathIsEmpty) {
  ASSERT_TRUE(hpp::PathBuilder{}.Build().IsEmpty());
  ASSERT_FALSE(hpp::PathBuilder{}.AddRect({0, 0, 1, 1}).Build().IsEmpty());
}

TEST_P(InteropPlaygroundTest, CanTransformPaths) {
  const auto path = hpp::PathBuilder{}.AddRect({0, 0, 10, 10}).Build();
  ImpellerMatrix transform = {
      // clang-format off
      2.0, 0.0, 0.0, 0.0,
      0.0, 3.0, 0.0, 0.0,
      0.0, 0.0, 1.0, 0.0,
      5.0, 7.0, 0.0, 1.0,
      // clang-format on
  };
  const auto bounds = path.Transformed(transform).GetTightBounds();
  ASSERT_FLOAT_EQ(bounds.x, 5);
  ASSERT_FLOAT_EQ(bounds.y, 7);
  ASSERT_FLOAT_EQ(bounds.width, 20);
  ASSERT_FLOAT_EQ(bounds.height, 30);
}

TEST_P(InteropPlaygroundTest, CanPerformPathOps) {
  const auto a = hpp::PathBuilder{}.AddRect({0, 0, 100, 100}).Build();
  const auto b = hpp::PathBuilder{}.AddRect({50, 50, 100, 100}).Build();

  const auto intersect = a.Op(b, kImpellerPathOpIntersect);
  ASSERT_TRUE(intersect);
  auto bounds = intersect.GetTightBounds();
  ASSERT_FLOAT_EQ(bounds.x, 50);
  ASSERT_FLOAT_EQ(bounds.width, 50);

  const auto join = a.Op(b, kImpellerPathOpUnion);
  bounds = join.GetTightBounds();
  ASSERT_FLOAT_EQ(bounds.width, 150);
  ASSERT_TRUE(join.Contains({125, 125}));

  const auto difference = a.Op(b, kImpellerPathOpDifference);
  ASSERT_TRUE(difference.Contains({25, 25}));
  ASSERT_FALSE(difference.Contains({75, 75}));
  ASSERT_FALSE(difference.Contains({125, 125}));

  const auto reverse = a.Op(b, kImpellerPathOpReverseDifference);
  ASSERT_FALSE(reverse.Contains({25, 25}));
  ASSERT_TRUE(reverse.Contains({125, 125}));

  const auto xor_path = a.Op(b, kImpellerPathOpXor);
  ASSERT_TRUE(xor_path.Contains({25, 25}));
  ASSERT_FALSE(xor_path.Contains({75, 75}));
  ASSERT_TRUE(xor_path.Contains({125, 125}));

  const auto none = a.Op(hpp::PathBuilder{}.AddRect({200, 200, 10, 10}).Build(),
                         kImpellerPathOpIntersect);
  ASSERT_TRUE(none);
  ASSERT_TRUE(none.IsEmpty());
}

TEST_P(InteropPlaygroundTest, CanStrokePaths) {
  const auto line =
      hpp::PathBuilder{}.MoveTo({10, 50}).LineTo({110, 50}).Build();
  ImpellerStrokeParameters stroke = {};
  stroke.width = 10;
  stroke.cap = kImpellerStrokeCapButt;
  stroke.join = kImpellerStrokeJoinMiter;
  stroke.miter_limit = 4;
  const auto outline = line.Stroked(stroke);
  ASSERT_TRUE(outline);
  auto bounds = outline.GetTightBounds();
  ASSERT_FLOAT_EQ(bounds.x, 10);
  ASSERT_FLOAT_EQ(bounds.y, 45);
  ASSERT_FLOAT_EQ(bounds.width, 100);
  ASSERT_FLOAT_EQ(bounds.height, 10);
  ASSERT_TRUE(outline.Contains({50, 52}));
  ASSERT_FALSE(outline.Contains({50, 57}));

  stroke.cap = kImpellerStrokeCapSquare;
  bounds = line.Stroked(stroke).GetTightBounds();
  ASSERT_FLOAT_EQ(bounds.x, 5);
  ASSERT_FLOAT_EQ(bounds.width, 110);

  // Hairlines have no outline.
  stroke.width = 0;
  ASSERT_FALSE(line.Stroked(stroke));
}

TEST_P(InteropPlaygroundTest, CanDashPaths) {
  const auto line = hpp::PathBuilder{}.MoveTo({0, 0}).LineTo({100, 0}).Build();
  const auto dashed = line.Dashed({10, 10});
  ASSERT_TRUE(dashed);
  // Five 10 unit dashes over 100 units.
  const auto bounds = dashed.GetTightBounds();
  ASSERT_FLOAT_EQ(bounds.x, 0);
  ASSERT_FLOAT_EQ(bounds.width, 90);

  ImpellerStrokeParameters stroke = {};
  stroke.width = 2;
  stroke.miter_limit = 4;
  const auto outline = dashed.Stroked(stroke);
  ASSERT_TRUE(outline.Contains({5, 0}));
  ASSERT_FALSE(outline.Contains({15, 0}));
  ASSERT_TRUE(outline.Contains({25, 0}));

  const auto shifted = line.Dashed({10, 10}, 5).Stroked(stroke);
  ASSERT_TRUE(shifted.Contains({2, 0}));
  ASSERT_FALSE(shifted.Contains({7, 0}));

  // Odd interval counts are invalid.
  {
    ScopedValidationDisable disable_validation;
    ASSERT_FALSE(line.Dashed({10, 10, 10}));
  }
}

TEST_P(InteropPlaygroundTest, CanAddPathsToBuilders) {
  const auto rect = hpp::PathBuilder{}.AddRect({0, 0, 10, 10}).Build();
  ImpellerMatrix translate = {
      // clang-format off
      1.0, 0.0, 0.0, 0.0,
      0.0, 1.0, 0.0, 0.0,
      0.0, 0.0, 1.0, 0.0,
      20.0, 0.0, 0.0, 1.0,
      // clang-format on
  };
  const auto both =
      hpp::PathBuilder{}.AddPath(rect).AddPath(rect, &translate).Build();
  const auto bounds = both.GetTightBounds();
  ASSERT_FLOAT_EQ(bounds.width, 30);
  ASSERT_TRUE(both.Contains({5, 5}));
  ASSERT_FALSE(both.Contains({15, 5}));
  ASSERT_TRUE(both.Contains({25, 5}));
}

TEST_P(InteropPlaygroundTest, CanAddSvgArcs) {
  // A half circle of radius 50 from (0, 50) to (100, 50).
  const auto clockwise = hpp::PathBuilder{}
                             .MoveTo({0, 50})
                             .SvgArcTo({50, 50}, 0, false, true, {100, 50})
                             .Build();
  auto bounds = clockwise.GetTightBounds();
  ASSERT_NEAR(bounds.x, 0, 1e-3);
  ASSERT_NEAR(bounds.y, 0, 1e-3);
  ASSERT_NEAR(bounds.width, 100, 1e-3);
  ASSERT_NEAR(bounds.height, 50, 1e-3);

  const auto counter_clockwise =
      hpp::PathBuilder{}
          .MoveTo({0, 50})
          .SvgArcTo({50, 50}, 0, false, false, {100, 50})
          .Build();
  bounds = counter_clockwise.GetTightBounds();
  ASSERT_NEAR(bounds.y, 50, 1e-3);
  ASSERT_NEAR(bounds.height, 50, 1e-3);

  // Radii too small to reach the end point are scaled up.
  const auto scaled = hpp::PathBuilder{}
                          .MoveTo({0, 50})
                          .SvgArcTo({10, 10}, 0, false, true, {100, 50})
                          .Build();
  bounds = scaled.GetTightBounds();
  ASSERT_NEAR(bounds.height, 50, 1e-3);
}

TEST_P(InteropPlaygroundTest, CanMeasurePaths) {
  const auto line = hpp::PathBuilder{}.MoveTo({0, 0}).LineTo({30, 40}).Build();
  hpp::PathMeasure measure(line);
  ASSERT_TRUE(measure);
  ASSERT_FLOAT_EQ(measure.GetLength(), 50);

  ImpellerPoint position = {};
  ImpellerPoint tangent = {};
  ASSERT_TRUE(measure.GetPositionAndTangent(25, position, tangent));
  ASSERT_FLOAT_EQ(position.x, 15);
  ASSERT_FLOAT_EQ(position.y, 20);
  ASSERT_FLOAT_EQ(tangent.x, 0.6f);
  ASSERT_FLOAT_EQ(tangent.y, 0.8f);

  // Distances are clamped to the contour.
  ASSERT_TRUE(measure.GetPositionAndTangent(100, position, tangent));
  ASSERT_FLOAT_EQ(position.x, 30);
  ASSERT_FLOAT_EQ(position.y, 40);

  const auto segment = measure.CreateSegment(10, 35);
  ASSERT_TRUE(segment);
  hpp::PathMeasure segment_measure(segment);
  ASSERT_NEAR(segment_measure.GetLength(), 25, 1e-4);
  const auto bounds = segment.GetTightBounds();
  ASSERT_NEAR(bounds.x, 6, 1e-4);
  ASSERT_NEAR(bounds.y, 8, 1e-4);

  // Reversed segments are NULL.
  ASSERT_FALSE(measure.CreateSegment(30, 20));

  ASSERT_FALSE(measure.NextContour());
  ASSERT_FLOAT_EQ(measure.GetLength(), 0);
  ASSERT_FALSE(measure.GetPositionAndTangent(0, position, tangent));
}

TEST_P(InteropPlaygroundTest, CanMeasureCircles) {
  const auto circle = hpp::PathBuilder{}.AddOval({0, 0, 100, 100}).Build();
  hpp::PathMeasure measure(circle);
  // Curves are measured by approximation.
  ASSERT_NEAR(measure.GetLength(), 100 * kPi, 1.0);
}

TEST_P(InteropPlaygroundTest, CanMeasureMultipleContours) {
  const auto path = hpp::PathBuilder{}
                        .MoveTo({0, 0})
                        .LineTo({10, 0})
                        .MoveTo({0, 10})
                        .LineTo({0, 30})
                        .LineTo({20, 30})
                        .Build();
  hpp::PathMeasure measure(path);
  ASSERT_FLOAT_EQ(measure.GetLength(), 10);
  ASSERT_TRUE(measure.NextContour());
  ASSERT_FLOAT_EQ(measure.GetLength(), 40);
  ASSERT_FALSE(measure.NextContour());

  // Forcing contours closed adds the closing segment.
  hpp::PathMeasure closed(path, true);
  ASSERT_TRUE(closed.NextContour());
  ASSERT_NEAR(closed.GetLength(), 40 + std::sqrt(800.0f), 1e-3);
}

TEST_P(InteropPlaygroundTest, CanRenderToTexturesAndReadBack) {
  auto context = GetHPPContext();
  const ImpellerISize size = {64, 32};
  auto texture = hpp::Texture::RenderTarget(context, size);
  ASSERT_TRUE(texture);
  auto surface = hpp::Surface::WithTexture(context, texture);
  ASSERT_TRUE(surface);

  // Top half red, bottom-left quarter translucent green, the rest clear.
  auto dl = hpp::DisplayListBuilder{}
                .DrawRect({0, 0, 64, 16},
                          hpp::Paint{}.SetColor({.red = 1.0, .alpha = 1.0}))
                .DrawRect({0, 16, 32, 16},
                          hpp::Paint{}.SetColor({.green = 1.0, .alpha = 0.5}))
                .Build();
  ASSERT_TRUE(surface.Draw(dl));

  std::vector<uint8_t> pixels(64 * 32 * 4);
  ASSERT_TRUE(texture.ReadPixels(context, nullptr, pixels.data(), 64 * 4));
  auto pixel = [&](int x, int y) {
    const auto* p = &pixels[(y * 64 + x) * 4];
    return std::array<uint8_t, 4>{p[0], p[1], p[2], p[3]};
  };
  ASSERT_EQ(pixel(10, 5), (std::array<uint8_t, 4>{255, 0, 0, 255}));
  // Premultiplied.
  ASSERT_NEAR(pixel(10, 25)[1], 128, 1);
  ASSERT_NEAR(pixel(10, 25)[3], 128, 1);
  ASSERT_EQ(pixel(10, 25)[0], 0u);
  ASSERT_EQ(pixel(50, 25), (std::array<uint8_t, 4>{0, 0, 0, 0}));

  // Read a region with padded rows.
  const ImpellerIRect region = {30, 14, 4, 4};
  std::vector<uint8_t> sub(4 * 32, 0xAB);
  ASSERT_TRUE(texture.ReadPixels(context, &region, sub.data(), 32));
  // (30, 14) is red, (33, 17) is clear.
  ASSERT_EQ(sub[0], 255u);
  ASSERT_EQ(sub[3], 255u);
  ASSERT_EQ(sub[3 * 32 + 3 * 4 + 3], 0u);
  // Row padding is left untouched.
  ASSERT_EQ(sub[16], 0xABu);

  // Regions outside the texture are rejected.
  {
    ScopedValidationDisable disable_validation;
    const ImpellerIRect outside = {60, 0, 8, 8};
    ASSERT_FALSE(texture.ReadPixels(context, &outside, sub.data(), 32));
  }

  // The texture can be drawn into another display list.
  auto copy = hpp::Texture::RenderTarget(context, size);
  auto copy_surface = hpp::Surface::WithTexture(context, copy);
  ASSERT_TRUE(copy_surface.Draw(
      hpp::DisplayListBuilder{}
          .DrawTexture(texture, {0, 0}, kImpellerTextureSamplingNearestNeighbor,
                       hpp::Paint{})
          .Build()));
  std::vector<uint8_t> copied(64 * 32 * 4);
  ASSERT_TRUE(copy.ReadPixels(context, nullptr, copied.data(), 64 * 4));
  ASSERT_EQ(copied[(5 * 64 + 10) * 4 + 0], 255u);
  ASSERT_EQ(copied[(25 * 64 + 50) * 4 + 3], 0u);
}

static hpp::Typeface LoadFixtureTypeface(const char* name) {
  auto fixture = flutter::testing::OpenFixtureAsMapping(name);
  if (!fixture) {
    return hpp::Typeface(nullptr, hpp::AdoptTag::kAdopt);
  }
  auto mapping = std::make_unique<hpp::Mapping>(
      fixture->GetMapping(), fixture->GetSize(),
      [data = std::shared_ptr<fml::Mapping>(std::move(fixture))]() {});
  return hpp::Typeface::WithData(std::move(mapping));
}

static constexpr uint32_t MakeTag(char a, char b, char c, char d) {
  return (static_cast<uint32_t>(a) << 24) | (static_cast<uint32_t>(b) << 16) |
         (static_cast<uint32_t>(c) << 8) | static_cast<uint32_t>(d);
}

// The first glyph whose ink is at least the given size.
static uint16_t FindInkedGlyph(const hpp::Font& font, float min_size) {
  for (uint16_t glyph = 1u; glyph < 500u; glyph++) {
    const auto bounds = font.GetGlyphBounds({glyph})[0];
    if (bounds.width >= min_size && bounds.height >= min_size) {
      return glyph;
    }
  }
  return 0u;
}

TEST_P(InteropPlaygroundTest, CanCreateTypefaces) {
  auto typeface = LoadFixtureTypeface("Roboto-Regular.ttf");
  ASSERT_TRUE(typeface);
  ASSERT_EQ(typeface.GetUnitsPerEm(), 2048u);
  // The head table is always 54 bytes.
  ASSERT_EQ(typeface.CopyTableData(MakeTag('h', 'e', 'a', 'd')).size(), 54u);
  ASSERT_TRUE(typeface.CopyTableData(MakeTag('z', 'z', 'z', 'z')).empty());

  auto fixture = flutter::testing::OpenFixtureAsMapping("Roboto-Regular.ttf");
  uint32_t face_index = 99u;
  const auto data = typeface.CopyData(&face_index);
  ASSERT_EQ(data.size(), fixture->GetSize());
  ASSERT_EQ(face_index, 0u);
  ASSERT_EQ(::memcmp(data.data(), fixture->GetMapping(), data.size()), 0);

  {
    ScopedValidationDisable disable_validation;
    const uint8_t garbage[] = {1, 2, 3, 4};
    ASSERT_FALSE(hpp::Typeface::WithData(
        std::make_unique<hpp::Mapping>(garbage, sizeof(garbage), nullptr)));
  }
}

TEST_P(InteropPlaygroundTest, GlyphPathsMatchGlyphBounds) {
  auto typeface = LoadFixtureTypeface("Roboto-Regular.ttf");
  hpp::Font font(typeface, 100);
  const auto glyph = FindInkedGlyph(font, 40);
  ASSERT_NE(glyph, 0u);
  const auto path = font.CreateGlyphPath(glyph);
  ASSERT_TRUE(path);
  const auto path_bounds = path.GetTightBounds();
  const auto bounds = font.GetGlyphBounds({glyph})[0];
  // Paths are Y-down with the origin on the baseline.
  ASSERT_LT(bounds.y, -50);
  // Glyph bounds are rounded out to whole pixels.
  ASSERT_NEAR(path_bounds.x, bounds.x, 2.0);
  ASSERT_NEAR(path_bounds.y, bounds.y, 2.0);
  ASSERT_NEAR(path_bounds.width, bounds.width, 2.0);
  ASSERT_NEAR(path_bounds.height, bounds.height, 2.0);

  // Synthetic styles change the outlines.
  font.SetEmbolden(true);
  ASSERT_GT(font.CreateGlyphPath(glyph).GetTightBounds().width,
            path_bounds.width);
  font.SetEmbolden(false);
  font.SetSkewX(-0.25f);
  const auto skewed = font.CreateGlyphPath(glyph).GetTightBounds();
  ASSERT_GT(skewed.width, path_bounds.width);
}

TEST_P(InteropPlaygroundTest, CanApplyFontVariations) {
  auto typeface = LoadFixtureTypeface("RobotoSlab-VariableFont_wght.ttf");
  ASSERT_TRUE(typeface);
  const auto wght = MakeTag('w', 'g', 'h', 't');
  auto thin = typeface.WithVariations({{wght, 100}});
  auto black = typeface.WithVariations({{wght, 900}});
  ASSERT_TRUE(thin);
  ASSERT_TRUE(black);
  // Heavier weights are wider.
  const auto glyph = FindInkedGlyph(hpp::Font(thin, 100), 40);
  ASSERT_NE(glyph, 0u);
  const auto thin_bounds =
      hpp::Font(thin, 100).CreateGlyphPath(glyph).GetTightBounds();
  const auto black_bounds =
      hpp::Font(black, 100).CreateGlyphPath(glyph).GetTightBounds();
  ASSERT_GT(black_bounds.width, thin_bounds.width + 1);
}

TEST_P(InteropPlaygroundTest, CanDrawGlyphs) {
  auto typeface = LoadFixtureTypeface("ahem.ttf");
  ASSERT_TRUE(typeface);
  hpp::Font font(typeface, 20);
  // Find a glyph with ink. Every Ahem glyph with ink is an em box from 0.8em
  // above the baseline to 0.2em below it.
  const auto glyph = FindInkedGlyph(font, 19);
  ASSERT_NE(glyph, 0u);

  auto context = GetHPPContext();
  auto texture = hpp::Texture::RenderTarget(context, {64, 64});
  auto surface = hpp::Surface::WithTexture(context, texture);
  ASSERT_TRUE(surface);
  // Two glyphs at (10, 40) and (40, 40): boxes over x 10..30 and 40..60,
  // y 24..44.
  auto paint = hpp::Paint{}.SetColor({.blue = 1.0, .alpha = 1.0});
  auto dl =
      hpp::DisplayListBuilder{}
          .DrawGlyphs(font, {glyph, glyph}, {{0, 0}, {30, 0}}, {10, 40}, paint)
          .Build();
  ASSERT_TRUE(surface.Draw(dl));
  std::vector<uint8_t> pixels(64 * 64 * 4);
  ASSERT_TRUE(texture.ReadPixels(context, nullptr, pixels.data(), 64 * 4));
  auto alpha = [&](int x, int y) { return pixels[(y * 64 + x) * 4 + 3]; };
  auto blue = [&](int x, int y) { return pixels[(y * 64 + x) * 4 + 2]; };
  ASSERT_EQ(alpha(20, 34), 255u);
  ASSERT_EQ(blue(20, 34), 255u);
  ASSERT_EQ(alpha(50, 34), 255u);
  ASSERT_EQ(alpha(35, 34), 0u);
  ASSERT_EQ(alpha(20, 20), 0u);
  ASSERT_EQ(alpha(20, 48), 0u);

  // Color sources draw outlines.
  const ImpellerColor colors[] = {{.red = 1.0, .alpha = 1.0},
                                  {.red = 1.0, .alpha = 1.0}};
  const float stops[] = {0.0, 1.0};
  auto gradient = hpp::ColorSource::LinearGradient(
      {0, 0}, {64, 0}, 2, colors, stops, kImpellerTileModeClamp);
  auto gradient_paint = hpp::Paint{};
  gradient_paint.SetColorSource(gradient);
  auto gradient_dl =
      hpp::DisplayListBuilder{}
          .DrawGlyphs(font, {glyph}, {{0, 0}}, {10, 40}, gradient_paint)
          .Build();
  ASSERT_TRUE(surface.Draw(gradient_dl));
  ASSERT_TRUE(texture.ReadPixels(context, nullptr, pixels.data(), 64 * 4));
  ASSERT_EQ(alpha(20, 34), 255u);
  ASSERT_GE(pixels[(34 * 64 + 20) * 4 + 0], 250u);
  ASSERT_EQ(alpha(50, 34), 0u);
}

static bool RegisterFixtureFont(hpp::TypographyContext& context,
                                const char* name,
                                const char* alias) {
  auto fixture = flutter::testing::OpenFixtureAsMapping(name);
  if (!fixture) {
    return false;
  }
  auto mapping = std::make_unique<hpp::Mapping>(
      fixture->GetMapping(), fixture->GetSize(),
      [data = std::shared_ptr<fml::Mapping>(std::move(fixture))]() {});
  return context.RegisterFont(std::move(mapping), alias);
}

TEST_P(InteropPlaygroundTest, CanMatchRegisteredTypefaces) {
  hpp::TypographyContext context;
  ASSERT_TRUE(RegisterFixtureFont(context, "Roboto-Regular.ttf", "Faces"));
  ASSERT_TRUE(RegisterFixtureFont(context, "Roboto-Medium.ttf", "Faces"));
  ASSERT_TRUE(RegisterFixtureFont(context, "ahem.ttf", "MyAhem"));

  const ImpellerTypefaceStyle normal = {400, 5, kImpellerFontSlantUpright};
  auto ahem = hpp::MatchTypeface(context, "MyAhem", normal);
  ASSERT_TRUE(ahem);
  ASSERT_EQ(ahem.GetUnitsPerEm(), 1000u);

  auto regular = hpp::MatchTypeface(context, "Faces", normal);
  ASSERT_TRUE(regular);
  auto style = regular.GetStyle();
  ASSERT_EQ(style.weight, 400u);
  ASSERT_EQ(style.width, 5u);
  ASSERT_EQ(style.slant, kImpellerFontSlantUpright);
  ASSERT_EQ(regular.GetFamilyName(), "Roboto");

  auto medium =
      hpp::MatchTypeface(context, "Faces", {600, 5, kImpellerFontSlantUpright});
  ASSERT_TRUE(medium);
  ASSERT_EQ(medium.GetStyle().weight, 500u);

  ASSERT_FALSE(hpp::MatchTypeface(context, "No Such Family", normal));

  const auto names = context.GetFamilyNames();
  ASSERT_GE(names.size(), 2u);
  ASSERT_NE(std::find(names.begin(), names.end(), "Faces"), names.end());
  ASSERT_NE(std::find(names.begin(), names.end(), "MyAhem"), names.end());
}

TEST_P(InteropPlaygroundTest, CanListTheStylesOfFamilies) {
  hpp::TypographyContext context;
  ASSERT_TRUE(RegisterFixtureFont(context, "Roboto-Regular.ttf", "Faces"));
  ASSERT_TRUE(RegisterFixtureFont(context, "Roboto-Medium.ttf", "Faces"));

  auto styles = context.GetFamilyStyles("Faces");
  ASSERT_EQ(styles.size(), 2u);
  std::sort(styles.begin(), styles.end(),
            [](const auto& a, const auto& b) { return a.weight < b.weight; });
  ASSERT_EQ(styles[0].weight, 400u);
  ASSERT_EQ(styles[1].weight, 500u);
  for (const auto& style : styles) {
    ASSERT_EQ(style.width, 5u);
    ASSERT_EQ(style.slant, kImpellerFontSlantUpright);
  }

  ASSERT_TRUE(context.GetFamilyStyles("No Such Family").empty());
}

TEST_P(InteropPlaygroundTest, CanMatchCharactersInRegisteredFonts) {
  const ImpellerTypefaceStyle kNormalStyle = {400, 5,
                                              kImpellerFontSlantUpright};
  hpp::TypographyContext context;
  ASSERT_TRUE(RegisterFixtureFont(context, "Roboto-Regular.ttf", "Text"));
  ASSERT_TRUE(RegisterFixtureFont(context, "NotoColorEmoji.ttf", "Emoji"));

  // The preferred family has the glyph.
  auto latin = hpp::MatchCharacter(context, "Text", kNormalStyle, "en-US", 'A');
  ASSERT_TRUE(latin);
  ASSERT_EQ(latin.GetFamilyName(), "Roboto");

  // Falls back to a registered family that has the glyph.
  auto emoji =
      hpp::MatchCharacter(context, "Text", kNormalStyle, nullptr, 0x1F600);
  ASSERT_TRUE(emoji);
  ASSERT_EQ(emoji.GetFamilyName(), "Noto Color Emoji");
}

static hpp::ImageDecoder LoadFixtureImage(const char* name) {
  auto fixture = flutter::testing::OpenFixtureAsMapping(name);
  if (!fixture) {
    return hpp::ImageDecoder(nullptr, hpp::AdoptTag::kAdopt);
  }
  auto mapping = std::make_unique<hpp::Mapping>(
      fixture->GetMapping(), fixture->GetSize(),
      [data = std::shared_ptr<fml::Mapping>(std::move(fixture))]() {});
  return hpp::ImageDecoder::WithData(std::move(mapping));
}

TEST_P(InteropPlaygroundTest, CanDecodeImages) {
  auto jpeg = LoadFixtureImage("boston.jpg");
  ASSERT_TRUE(jpeg);
  auto size = jpeg.GetSize();
  ASSERT_EQ(size.width, 983);
  ASSERT_EQ(size.height, 609);
  std::vector<uint8_t> pixels(size.width * size.height * 4);
  ASSERT_TRUE(jpeg.Decode(nullptr, pixels.data(), size.width * 4));
  ASSERT_EQ(pixels[(300 * size.width + 500) * 4 + 3], 255u);

  // Scaled decodes produce the requested size.
  const ImpellerISize scaled = {100, 62};
  std::vector<uint8_t> small(100 * 62 * 4, 0u);
  ASSERT_TRUE(jpeg.Decode(&scaled, small.data(), 100 * 4));
  ASSERT_EQ(small[(61 * 100 + 99) * 4 + 3], 255u);
  // The scaled image looks like the original.
  for (int channel = 0; channel < 3; channel++) {
    const int original = pixels[(300 * size.width + 500) * 4 + channel];
    const int reduced = small[(30 * 100 + 50) * 4 + channel];
    ASSERT_NEAR(original, reduced, 40);
  }

  auto png = LoadFixtureImage("table_mountain_nx.png");
  ASSERT_TRUE(png);
  size = png.GetSize();
  ASSERT_EQ(size.width, 256);
  ASSERT_EQ(size.height, 256);

  {
    ScopedValidationDisable disable_validation;
    const uint8_t garbage[] = {1, 2, 3, 4};
    ASSERT_FALSE(hpp::ImageDecoder::WithData(
        std::make_unique<hpp::Mapping>(garbage, sizeof(garbage), nullptr)));
  }
}

TEST_P(InteropPlaygroundTest, CanEncodeImages) {
  // A 4x2 image: opaque red, green, blue, white, then half transparent
  // (premultiplied) red, and three clear pixels.
  const std::vector<uint8_t> pixels = {
      255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255,
      128, 0, 0, 128, 0, 0,   0, 0,   0, 0, 0,   0,   0,   0,   0,   0,
  };
  const ImpellerISize size = {4, 2};

  std::vector<uint8_t> png;
  ASSERT_TRUE(hpp::EncodeImage(pixels.data(), size, 16, kImpellerImageFormatPNG,
                               9, png));
  ASSERT_GT(png.size(), 8u);
  ASSERT_EQ(png[1], 'P');
  auto decoder = hpp::ImageDecoder::WithData(
      std::make_unique<hpp::Mapping>(png.data(), png.size(), nullptr));
  ASSERT_TRUE(decoder);
  ASSERT_EQ(decoder.GetSize().width, 4);
  std::vector<uint8_t> decoded(32);
  ASSERT_TRUE(decoder.Decode(nullptr, decoded.data(), 16));
  for (size_t i = 0; i < pixels.size(); i++) {
    ASSERT_NEAR(decoded[i], pixels[i], 1) << "byte " << i;
  }

  std::vector<uint8_t> jpeg;
  ASSERT_TRUE(hpp::EncodeImage(pixels.data(), size, 16,
                               kImpellerImageFormatJPEG, 90, jpeg));
  ASSERT_EQ(jpeg[0], 0xFF);
  ASSERT_EQ(jpeg[1], 0xD8);

  std::vector<uint8_t> webp;
  ASSERT_TRUE(hpp::EncodeImage(pixels.data(), size, 16,
                               kImpellerImageFormatWebP, 100, webp));
  auto webp_decoder = hpp::ImageDecoder::WithData(
      std::make_unique<hpp::Mapping>(webp.data(), webp.size(), nullptr));
  ASSERT_TRUE(webp_decoder);
  ASSERT_TRUE(webp_decoder.Decode(nullptr, decoded.data(), 16));
  // Lossless.
  ASSERT_EQ(decoded[0], 255u);
  ASSERT_EQ(decoded[5], 255u);
}

TEST_P(InteropPlaygroundTest, PNGCompressionLevelChangesTheSize) {
  const ImpellerISize size = {64, 64};
  const std::vector<uint8_t> pixels(size.width * size.height * 4, 255);
  std::vector<uint8_t> stored;
  ASSERT_TRUE(hpp::EncodeImage(pixels.data(), size, size.width * 4,
                               kImpellerImageFormatPNG, 0, stored));
  std::vector<uint8_t> compressed;
  ASSERT_TRUE(hpp::EncodeImage(pixels.data(), size, size.width * 4,
                               kImpellerImageFormatPNG, 9, compressed));
  ASSERT_GT(stored.size(), pixels.size());
  ASSERT_LT(compressed.size(), stored.size() / 10);
}

TEST_P(InteropPlaygroundTest, CanControlEllipses) {
  hpp::TypographyContext context;
  auto style = hpp::ParagraphStyle{};
  style.SetFontSize(50);
  style.SetForeground(hpp::Paint{}.SetColor({.red = 1.0, .alpha = 1.0}));
  const auto text = std::string{"The quick brown fox jumped over the lazy dog"};
  style.SetEllipsis("🐶");
  auto para1 =
      hpp::ParagraphBuilder{context}.PushStyle(style).AddText(text).Build(250);
  style.SetForeground(hpp::Paint{}.SetColor({.green = 1.0, .alpha = 1.0}));
  style.SetEllipsis(nullptr);
  auto para2 =
      hpp::ParagraphBuilder{context}.PushStyle(style).AddText(text).Build(250);
  auto dl = hpp::DisplayListBuilder{}
                .DrawParagraph(para1, {100, 100})
                .DrawParagraph(para2, {100, 200})
                .Build();
  ASSERT_TRUE(
      OpenPlaygroundHere([&](const auto& context, const auto& surface) -> bool {
        hpp::Surface window(surface.GetC());
        window.Draw(dl);
        return true;
      }));
}

TEST_P(InteropPlaygroundTest, CanCreateFragmentProgramColorFilters) {
  if (GetBackend() == PlaygroundBackend::kOpenGLES ||
      GetBackend() == PlaygroundBackend::kOpenGLESSDF) {
    GTEST_SKIP() << "See: https://github.com/flutter/flutter/issues/188882";
  }

  auto iplr = OpenAssetAsHPPMapping("interop_runtime_stage_cs.frag.iplr");
  ASSERT_TRUE(!!iplr);
  auto program = hpp::FragmentProgram::WithData(std::move(iplr));
  ASSERT_TRUE(program);
  auto context = GetHPPContext();
  auto filter =
      hpp::ImageFilter::FragmentProgram(context, program, {}, nullptr);
  ASSERT_TRUE(filter);
  auto bay_bridge = OpenAssetAsHPPTexture("bay_bridge.jpg");
  ASSERT_TRUE(bay_bridge);

  float size_data[4] = {500, 500};
  auto uniform_data = hpp::Mapping{reinterpret_cast<const uint8_t*>(&size_data),
                                   sizeof(size_data), nullptr};

  auto dl = hpp::DisplayListBuilder{}
                .DrawRect({10, 10, 500, 500},
                          hpp::Paint{}
                              .SetColor({1.0, 1.0, 1.0, 1.0})
                              .SetColorSource(hpp::ColorSource::FragmentProgram(
                                  context,             //
                                  program,             //
                                  {bay_bridge.Get()},  // samplers
                                  &uniform_data        // uniform data
                                  )))
                .Build();
  ASSERT_TRUE(
      OpenPlaygroundHere([&](const auto& context, const auto& surface) -> bool {
        hpp::Surface window(surface.GetC());
        window.Draw(dl);
        return true;
      }));
}

TEST_P(InteropPlaygroundTest, MappingsReleaseTheirDataOnDestruction) {
  bool deleted = false;
  {
    hpp::Mapping mapping(nullptr, 0, [&deleted]() { deleted = true; });
  }
  ASSERT_TRUE(deleted);
}

}  // namespace impeller::interop::testing
