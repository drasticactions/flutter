// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "impeller/toolkit/interop/typography_context.h"

#include <mutex>
#include <set>

#include "flutter/fml/build_config.h"
#include "flutter/txt/src/txt/platform.h"
#include "impeller/base/validation.h"

// Wasm builds compile the ICU data directly into libicu (see
// //flutter/third_party/icu:icudata), so there is nothing to load at runtime.
#if !defined(FML_OS_EMSCRIPTEN)
#include "flutter/fml/icu_util.h"
#include "impeller/toolkit/interop/embedded_icu_data.h"
#endif  // !defined(FML_OS_EMSCRIPTEN)

namespace impeller::interop {

TypographyContext::TypographyContext()
    : collection_(std::make_shared<txt::FontCollection>()) {
#if !defined(FML_OS_EMSCRIPTEN)
  static std::once_flag sICUInitOnceFlag;
  std::call_once(sICUInitOnceFlag, []() {
    auto icu_data = std::make_unique<fml::NonOwnedMapping>(
        impeller_embedded_icu_data_data, impeller_embedded_icu_data_length);
    fml::icu::InitializeICUFromMapping(std::move(icu_data));
  });
#endif  // !defined(FML_OS_EMSCRIPTEN)
  // The fallback for all fonts. Looks in platform specific locations.
  collection_->SetupDefaultFontManager(0u);

  // Looks for fonts in user supplied blobs.
  asset_font_manager_ = sk_make_sp<skia::textlayout::TypefaceFontProvider>();
  collection_->SetAssetFontManager(asset_font_manager_);
}

TypographyContext::~TypographyContext() = default;

bool TypographyContext::IsValid() const {
  return !!collection_;
}

const std::shared_ptr<txt::FontCollection>&
TypographyContext::GetFontCollection() const {
  return collection_;
}

static sk_sp<SkTypeface> CreateTypefaceFromFontData(
    std::unique_ptr<fml::Mapping> font_data) {
  if (!font_data) {
    VALIDATION_LOG << "Invalid font data.";
    return nullptr;
  }
  auto sk_data_context = font_data.release();
  auto sk_data = SkData::MakeWithProc(
      sk_data_context->GetMapping(),  // data ptr
      sk_data_context->GetSize(),     // data size
      [](const void*, void* context) {
        delete reinterpret_cast<decltype(sk_data_context)>(context);
      },               // release callback
      sk_data_context  // release callback context
  );
  auto sk_data_stream = SkMemoryStream::Make(sk_data);
  auto sk_typeface =
      txt::GetDefaultFontManager()->makeFromStream(std::move(sk_data_stream));
  if (!sk_typeface) {
    VALIDATION_LOG << "Could not create typeface with data.";
    return nullptr;
  }
  return sk_typeface;
}

bool TypographyContext::RegisterFont(std::unique_ptr<fml::Mapping> font_data,
                                     const char* family_name_alias) {
  auto typeface = CreateTypefaceFromFontData(std::move(font_data));
  if (typeface == nullptr) {
    return false;
  }
  size_t result = 0u;
  if (family_name_alias == nullptr) {
    result = asset_font_manager_->registerTypeface(std::move(typeface));
  } else {
    result = asset_font_manager_->registerTypeface(std::move(typeface),
                                                   SkString{family_name_alias});
  }
  family_names_valid_ = false;
  return result != 0;
}

// The managers in the order paragraphs use them: registered fonts, then the
// platform.
std::vector<sk_sp<SkFontMgr>> TypographyContext::GetFontManagers() const {
  return {asset_font_manager_, txt::GetDefaultFontManager()};
}

static bool HasGlyph(const sk_sp<SkTypeface>& typeface, uint32_t codepoint) {
  return typeface && typeface->unicharToGlyph(codepoint) != 0;
}

sk_sp<SkTypeface> TypographyContext::MatchTypeface(
    const char* family,
    const SkFontStyle& style) const {
  const auto managers = GetFontManagers();
  if (family) {
    for (const auto& manager : managers) {
      if (auto typeface = manager->matchFamilyStyle(family, style)) {
        return typeface;
      }
    }
    return nullptr;
  }
  // The platform's own default, such as fontconfig's sans-serif.
  if (auto typeface =
          txt::GetDefaultFontManager()->legacyMakeTypeface(nullptr, style)) {
    return typeface;
  }
  for (const auto& name : txt::GetDefaultFontFamilies()) {
    for (const auto& manager : managers) {
      if (auto typeface = manager->matchFamilyStyle(name.c_str(), style)) {
        return typeface;
      }
    }
  }
  for (const auto& manager : managers) {
    if (auto typeface = manager->legacyMakeTypeface(nullptr, style)) {
      return typeface;
    }
  }
  return nullptr;
}

sk_sp<SkTypeface> TypographyContext::MatchCharacter(const char* family,
                                                    const SkFontStyle& style,
                                                    const char* bcp47_locale,
                                                    uint32_t codepoint) const {
  if (family) {
    auto preferred = MatchTypeface(family, style);
    if (HasGlyph(preferred, codepoint)) {
      return preferred;
    }
  }
  const char* locales[] = {bcp47_locale};
  const int locale_count = bcp47_locale ? 1 : 0;
  for (const auto& manager : GetFontManagers()) {
    if (auto typeface = manager->matchFamilyStyleCharacter(
            family, style, locale_count ? locales : nullptr, locale_count,
            static_cast<SkUnichar>(codepoint))) {
      return typeface;
    }
    // Registered fonts can't be matched by character, so look at each family.
    if (manager == asset_font_manager_) {
      for (int i = 0; i < manager->countFamilies(); i++) {
        SkString name;
        manager->getFamilyName(i, &name);
        auto typeface = manager->matchFamilyStyle(name.c_str(), style);
        if (HasGlyph(typeface, codepoint)) {
          return typeface;
        }
      }
    }
  }
  return nullptr;
}

std::vector<SkFontStyle> TypographyContext::GetFamilyStyles(
    const char* family) const {
  std::vector<SkFontStyle> styles;
  for (const auto& manager : GetFontManagers()) {
    auto set = manager->matchFamily(family);
    if (!set || set->count() == 0) {
      continue;
    }
    for (int i = 0; i < set->count(); i++) {
      SkFontStyle style;
      set->getStyle(i, &style, nullptr);
      styles.push_back(style);
    }
    break;
  }
  return styles;
}

const std::vector<std::string>& TypographyContext::GetFamilyNames() const {
  if (family_names_valid_) {
    return family_names_;
  }
  family_names_.clear();
  std::set<std::string> seen;
  for (const auto& manager : GetFontManagers()) {
    for (int i = 0; i < manager->countFamilies(); i++) {
      SkString name;
      manager->getFamilyName(i, &name);
      std::string family{name.c_str(), name.size()};
      if (seen.insert(family).second) {
        family_names_.push_back(std::move(family));
      }
    }
  }
  family_names_valid_ = true;
  return family_names_;
}

}  // namespace impeller::interop
