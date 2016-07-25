# Component makefile for extras/ili9341

INC_DIRS += $(ili9341_ROOT) $(ili9341_ROOT)/adafruit

# args for passing into compile rule generation
ili9341_SRC_DIR =  $(ili9341_ROOT) $(ili9341_ROOT)/adafruit

$(eval $(call component_compile_rules,ili9341))

