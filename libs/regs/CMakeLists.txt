# satlink::regs — register/address headers generated from icd/ (see tools/regmap).
# Regenerate with: python3 tools/regmap/regmap_gen.py
# @implements SRS-ICD-001

add_library(satlink_regs INTERFACE)
add_library(satlink::regs ALIAS satlink_regs)
target_include_directories(satlink_regs INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
