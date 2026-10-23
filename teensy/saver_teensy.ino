// SAVER: manually positioned mannequin/bench demonstration; not clinical software.
// Automatic HOME/search success is never simulated. Read README_demo.md first.
#include "src/demo/demo_app.h"
void setup() { saver_demo::instance().begin(); }
void loop() { saver_demo::instance().tick(); }
