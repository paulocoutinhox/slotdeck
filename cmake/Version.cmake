set(SLOTDECK_PRODUCT_NAME "SlotDeck")
set(SLOTDECK_ORGANIZATION_NAME "SlotDeck")
set(SLOTDECK_ORGANIZATION_DOMAIN "slotdeck.local")
set(SLOTDECK_BUNDLE_IDENTIFIER "com.paulocoutinho.slotdeck")

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/app/BuildInfo.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/BuildInfo.h"
    @ONLY
)
