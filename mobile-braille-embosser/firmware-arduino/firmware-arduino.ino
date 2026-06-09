/*
 * Graham Brailler — Arduino Micro (skeleton_v5, direct-pin keyboard).
 * Logic is in src/*.cpp; this file only wires setup/loop (no src/ includes).
 * src/protocol.{h,c} are symlinks to ../shared/ — the single protocol source.
 */

void braillatron_setup(void);
void braillatron_loop(void);

void setup()
{
    braillatron_setup();
}

void loop()
{
    braillatron_loop();
}
