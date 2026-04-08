#include <Motoron.h>

// Create two independent shield objects
MotoronI2C mc1(16); // Shield 1 (Default Address)
MotoronI2C mc2(17); // Shield 2 (ADDR0 connected to high)

void setup()
{
  Wire.begin();
  
  // -- Initialize Shield 1 --
  mc1.reinitialize();   
  mc1.disableCrc();     
  mc1.clearResetFlag(); 
  mc1.disableCommandTimeout();
  mc1.setMaxAcceleration(1, 200);
  mc1.setMaxDeceleration(1, 300);
  mc1.setMaxAcceleration(2, 200);
  mc1.setMaxDeceleration(2, 300);
  mc1.setMaxAcceleration(3, 200);
  mc1.setMaxDeceleration(3, 300);

  // -- Initialize Shield 2 --
  mc2.reinitialize();   
  mc2.disableCrc();     
  mc2.clearResetFlag(); 
  mc2.disableCommandTimeout();
  // We only need to configure Motor 3 on this shield
  mc2.setMaxAcceleration(3, 200);
  mc2.setMaxDeceleration(3, 300);
}

void loop()
{
  // 1. Spin ALL 4 motors FORWARD at maximum speed
  mc1.setSpeed(1, 800); // Shield 1, Motor 1
  mc1.setSpeed(2, 800); // Shield 1, Motor 2
  mc1.setSpeed(3, 800); // Shield 1, Motor 3
  mc2.setSpeed(3, 800); // Shield 2, Motor 3
  delay(2000); 

  // 2. STOP all 4 motors
  mc1.setSpeed(1, 0);
  mc1.setSpeed(2, 0);
  mc1.setSpeed(3, 0);
  mc2.setSpeed(3, 0);
  delay(1000); 

  // 3. Spin ALL 4 motors BACKWARD at maximum speed
  mc1.setSpeed(1, -800);
  mc1.setSpeed(2, -800);
  mc1.setSpeed(3, -800);
  mc2.setSpeed(3, -800);
  delay(2000); 

  // 4. STOP all 4 motors
  mc1.setSpeed(1, 0);
  mc1.setSpeed(2, 0);
  mc1.setSpeed(3, 0);
  mc2.setSpeed(3, 0);
  delay(1000); 
}
