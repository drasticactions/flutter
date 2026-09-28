// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_IMPELLER_TOOLKIT_INTEROP_TYPOGRAPHY_CONTEXT_H_
#define FLUTTER_IMPELLER_TOOLKIT_INTEROP_TYPOGRAPHY_CONTEXT_H_

#include <memory>
#include <string>
#include <vector>

#include "flutter/fml/mapping.h"
#include "flutter/third_party/skia/modules/skparagraph/include/TypefaceFontProvider.h"
#include "flutter/txt/src/txt/font_collection.h"
#include "impeller/toolkit/interop/impeller.h"
#include "impeller/toolkit/interop/object.h"

namespace impeller::interop {

class TypographyContext final
    : public Object<TypographyContext,
                    IMPELLER_INTERNAL_HANDLE_NAME(ImpellerTypographyContext)> {
 public:
  TypographyContext();

  ~TypographyContext() override;

  TypographyContext(const TypographyContext&) = delete;

  TypographyContext& operator=(const TypographyContext&) = delete;

  bool IsValid() const;

  const std::shared_ptr<txt::FontCollection>& GetFontCollection() const;

  //----------------------------------------------------------------------------
  /// @brief      Registers custom font data. If an alias for the family name is
  ///             provided, subsequent lookups will need to use that same alias.
  ///             If not, the family name will be read from the font data.
  ///
  /// @param[in]  font_data          The font data
  /// @param[in]  family_name_alias  The family name alias
  ///
  /// @return     If the font data could be successfully registered.
  ///
  bool RegisterFont(std::unique_ptr<fml::Mapping> font_data,
                    const char* family_name_alias);

  //----------------------------------------------------------------------------
  /// @brief      Find the typeface of a family that best matches a style.
  ///             Registered fonts are searched before system fonts. A null
  ///             family matches the default family.
  ///
  sk_sp<SkTypeface> MatchTypeface(const char* family,
                                  const SkFontStyle& style) const;

  //----------------------------------------------------------------------------
  /// @brief      Find a typeface that has a glyph for a code point, preferring
  ///             the given family.
  ///
  sk_sp<SkTypeface> MatchCharacter(const char* family,
                                   const SkFontStyle& style,
                                   const char* bcp47_locale,
                                   uint32_t codepoint) const;

  //----------------------------------------------------------------------------
  /// @brief      The names of all font families, registered ones first,
  ///             without duplicates.
  ///
  const std::vector<std::string>& GetFamilyNames() const;

  //----------------------------------------------------------------------------
  /// @brief      The styles of the typefaces in a family, from the first font
  ///             manager that has the family.
  ///
  std::vector<SkFontStyle> GetFamilyStyles(const char* family) const;

 private:
  std::shared_ptr<txt::FontCollection> collection_;
  sk_sp<skia::textlayout::TypefaceFontProvider> asset_font_manager_;
  mutable std::vector<std::string> family_names_;

  sk_sp<SkFontMgr> default_font_manager_;

  std::vector<sk_sp<SkFontMgr>> GetFontManagers() const;
  mutable bool family_names_valid_ = false;
};

}  // namespace impeller::interop

#endif  // FLUTTER_IMPELLER_TOOLKIT_INTEROP_TYPOGRAPHY_CONTEXT_H_
