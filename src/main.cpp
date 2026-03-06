#include "config_check.h"
#include "role.h"
#include "Arduino.h"

// put your setup code here, to run once:
void setup() { role_setup(); }

int antiSpamCount = 0;

// put your main code here, to run repeatedly:
void loop() { role_loop(); }
