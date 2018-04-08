#include <lmic.h>
#include <hal/hal.h>

// This EUI must be in little-endian format, so least-significant-byte
// first. When copying an EUI from ttnctl output, this means to reverse
// the bytes. For TTN issued EUIs the last bytes should be 0xD5, 0xB3,
// 0x70.
static const u1_t PROGMEM APPEUI[8]={ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
void os_getArtEui (u1_t* buf) { memcpy_P(buf, APPEUI, 8);}

// This should also be in little endian format, see above.
static const u1_t PROGMEM DEVEUI[8]={ 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};
void os_getDevEui (u1_t* buf) { memcpy_P(buf, DEVEUI, 8);}

// This key should be in big endian format (or, since it is not really a
// number but a block of memory, endianness does not really apply). In
// practice, a key taken from ttnctl can be copied as-is.
// The key shown here is the semtech default key.
static const u1_t PROGMEM APPKEY[16] = { 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 };
void os_getDevKey (u1_t* buf) {  memcpy_P(buf, APPKEY, 16);}

// Schedule TX every this many seconds (might become longer due to duty
// cycle limitations).
const unsigned TX_INTERVAL = 60;

static osjob_t sendjob;

/* Pin mapping LoRa
| VCC | 3.3V |
| GND | GND |
| SCK | GPIO5 |
| MISO | GPIO19 |
| MOSI | GPIO27 |
| NSS | GPIO18 |
| NRESET | GPIO14 |
| DIO0 | GPIO26 |
| DIO1 | GPIO33 |
| DIO2 | GPIO32 |
*/
// GPIO5  -- SX1278's SCK
#define SCK 5
// GPIO19 -- SX1278's MISO
#define MISO MISO
// GPIO27 -- SX1278's MOSI
#define MOSI 27
// GPIO18 -- SX1278's CS
#define SS 18
// GPIO14 -- SX1278's RESET
#define RST 14
// GPIO26 -- SX1278's IRQ(Interrupt Request)
#define DIO0 26
// GPIO33 -- SX1278's 
#define DIO1 33
// GPIO32 -- SX1278's 
#define DIO2 32

// Pin mapping
const lmic_pinmap lmic_pins = {
    .nss = SS,
    .rxtx = LMIC_UNUSED_PIN,
    .rst = RST,
    .dio = {DIO0, DIO1, DIO2},
};
