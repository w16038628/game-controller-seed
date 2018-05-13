/* Game Controller */
#include <mbed.h>
#include <EthernetInterface.h>
#include <rtos.h>
#include <mbed_events.h>

#include <FXOS8700Q.h>
#include <C12832.h>
#include <stdbool.h>

/* display */
C12832 lcd(D11, D13, D12, D7, D10);

/*Speaker output*/
PwmOut speaker(D6);

/* event queue and thread support */
Thread dispatch;
EventQueue periodic;

/* Accelerometer */
I2C i2c(PTE25, PTE24);
FXOS8700QAccelerometer acc(i2c, FXOS8700CQ_SLAVE_ADDR1);

// Input from joystick up
InterruptIn joystickUp(PTB10);

/* Input from Potentiometers */
AnalogIn  left(A0);
AnalogIn right(A1);

// Red and Green LED's
DigitalOut red(LED_RED);
DigitalOut green(LED_GREEN);

/* Turn red LED on/off if lander crashed/not crashed */
void redLED(int state){
  //If lander crashed, turn on LED
  if (state == 0){
    red.write(1);
    // Else lander not crashed so turn off LED
  } else{
    red.write(0);
  }
}

void greenLED(int state){
  //If lander in flight, turn on LEDs
  if (state == 0){
    green.write(1);
    // Else lander not in flight, so turn off LED
  } else {
    green.write(0);
  }
}

/* User input states */
float throttleInputFloat;
int throttleInput;
float rollInput;
int boostOn = 1;


/* Task for polling sensors */
void user_input(void){
  motion_data_units_t a;
  acc.getAxis(a);

}


/* States from Lander */
float altitude = 0;
float fuelLevel;
int isFlying = 1;
int isCrashed = 0;
float orientation;
float Vx;
float Vy;



// Set IP address of lander and dash
SocketAddress lander("192.168.0.13",65200);
SocketAddress dash("192.168.0.13",65300);

EthernetInterface eth;
UDPSocket udp;
char buffer[512];
char line[80];

// Send message to the lander
int send(char *m, size_t s) {
  nsapi_size_or_error_t r = udp.sendto( lander, m, s);
  return r;
}
// Receive message from the lander
int receive(char *m, size_t s) {
  SocketAddress reply;
  nsapi_size_or_error_t r = udp.recvfrom(&reply, m,s );
  return r;
}

// Send message to the dashboard
int sendToDash(char *m, size_t s) {
  nsapi_size_or_error_t r = udp.sendto( dash, m, s);
  return r;
}

/* Task for synchronous UDP communications with lander */
void communications(void){

  // Get 0-1 value of throttle and roll as a float
  throttleInputFloat = left.read();
  rollInput = right.read();

  // Convert throttle input to 1-100 value and convert to int
  throttleInputFloat = throttleInputFloat * 100;
  throttleInput = throttleInputFloat;

  // Add 50 to the value of throttleInput if boost is activated
  if (boostOn == 0) {
    throttleInput = throttleInput + 50;
    // Else if the lander is crashed, set throttle to 0
  } else if (isCrashed == 1 || fuelLevel == 0) {
    throttleInput = 0;
  }

  // Get positive and negative value of roll
  rollInput = -1 + (rollInput *2);

  // Variable to store user input that will be sent to lander
  char buffer[512];
  // Variable to store comms received from the lander
  char outputBuffer[512];
  send(buffer, strlen(buffer));

  // Receive comms from lander
  size_t len = receive(outputBuffer, sizeof(outputBuffer));
  outputBuffer[len]='\0';


  //------------- Format comms from lander --------------//
  char *nextline, *line;
  // Iterate over each line of the message received from the lander
  for(
    line = strtok_r(outputBuffer, "\r\n", &nextline);
    line != NULL;
    line = strtok_r(NULL, "\r\n", &nextline)
  ) {
    char *key, *value;
    key = strtok(line, ":");
    value = strtok(NULL, ":");

    // If key is "altitude"
    if(strcmp(key, "altitude") == 0) {
      // Store the value in altitude variable
      altitude = atof(value);

      // If value of altitude is above 100 or the lander has landed/crashed turn off speaker else turn it on
      if (atof(value) > 100 || atof(value) <= 0){
        speaker.write(0);
      }   else {
        speaker.period(1.0/3000);
        speaker.write(0.5);
      }

    }
    // If key is "fuel"
    else if (strcmp(key, "fuel") == 0){
      // Store value in fuelLevel variable
      fuelLevel = atof(value);
      // If key is "flying"
    } else if (strcmp(key, "flying") == 0) {
      // Store value in isFlying variable
      isFlying = atoi(value);
      // If key is "crashed"
    } else if (strcmp(key, "crashed") == 0) {
      // Store value in isCrashed variable
      isCrashed = atoi(value);

      // If the lander is in flight, turn on green LED
      if (atoi(value) == 1){
        redLED(1);
        greenLED(0);
        // Else it has crashed so turn on red LED and turn off speaker
      } else {
        redLED(0);
        greenLED(1);
      }

      // If key is "orientation"
    } else if (strcmp(key, "orientation") == 0) {
      // Store value in orientation orientation variable
      orientation = atof(value);
      // If key is Vx
    } else if (strcmp(key, "Vx") == 0 ) {
      // Store value in "Vx"
      Vx = atof(value);
      // Else if key is "Vy"
    } else if (strcmp(key, "Vy") == 0) {
      // Store value in Vy
      Vy = atof(value);
    }

  }

  // Print user input to the lander
  lcd.locate(0,0);
  lcd.printf("Throttle: %d \nRoll: %.2f \n",throttleInput, rollInput);
  // Print the status of boost to the lcd
  if (boostOn == 0) {
   lcd.printf("Boost: ON\n");
 } else if (boostOn == 1) {
   lcd.printf("Boost: OFF\n");
 }



  /*TODO Create and format the message to send to the Lander */
  // Format throttle input received from left pot
  strcpy(buffer,"command:!\nthrottle:");
  sprintf(buffer,"%s%d",buffer,throttleInput);


  // Send formatted throttle input to lander
  send(buffer, strlen(buffer));


  // Format roll input received from right pot
  strcpy(buffer,"command:!\nroll:");
  sprintf(buffer,"%s%f",buffer,rollInput);
  // Send formatted roll input to lander
  send(buffer, strlen(buffer));
}



/* Task for asynchronous UDP communications with dashboard */
void dashboard(void){

// Buffer to store message to be sent to dashboard
char dashBuffer[512];

// Format and send altitude message to the dashboard
strcpy(dashBuffer, "altitude:");
sprintf(dashBuffer,"%s%.0f\n",dashBuffer,altitude);
sendToDash(dashBuffer, strlen(dashBuffer));

// Format and send fuel message to the dashboard
strcpy(dashBuffer, "fuel:");
sprintf(dashBuffer, "%s%.0f\n",dashBuffer,fuelLevel);
sendToDash(dashBuffer, strlen(dashBuffer));

// Format and send isFlying message to the dashboard
strcpy(dashBuffer, "flying:");
sprintf(dashBuffer,"%s%d\n",dashBuffer,isFlying);
sendToDash(dashBuffer, strlen(dashBuffer));

// Format and send orientation message to the dashboard
strcpy(dashBuffer, "orientation:");
sprintf(dashBuffer, "%s%.01f\n",dashBuffer,orientation);
sendToDash(dashBuffer, strlen(dashBuffer));

// Format and send Vx message to the dashboard
strcpy(dashBuffer, "Vx:");
sprintf(dashBuffer, "%s%.01f\n",dashBuffer,Vx);
sendToDash(dashBuffer, strlen(dashBuffer));

// Format and send Vy message to the dashboard
strcpy(dashBuffer, "Vy:");
sprintf(dashBuffer, "%s%.01f\n", dashBuffer,Vy);
sendToDash(dashBuffer, strlen(dashBuffer));

}

void throttleBoostOn() {
  boostOn = 0;
}

void throttleBoostOff() {
  boostOn = 1;
}

void checkGameStatus() {
  // Send a final message to the dashboard
  dashboard();
  // If the lander has crashed
  if (isCrashed == 1) {
    // Clear lcd and print game over message
        lcd.cls();
        lcd.locate(0,0);
        lcd.printf("GAME OVER");
        // Hold the program in an infinite loop
        while(1){}
        // Else if the lander has landed successfully
  } else if (isFlying == 0 && isCrashed == 0) {
    // Clear lcd and print successful landing message
        lcd.cls();
        lcd.locate(0,0);
        lcd.printf("SUCCESSFUL LANDING!\nReset to play again");

        // Hold the program in an infinite loop
        while(1){}
  }
}

int main() {

  acc.enable();

  /* ethernet connection : usually takes a few seconds */
  printf("connecting \n");
  eth.connect();
  /* write obtained IP address to serial monitor */
  const char *ip = eth.get_ip_address();
  printf("IP address is: %s\n", ip ? ip : "No IP");

  /* open udp for communications on the ethernet */
  udp.open( &eth);


  printf("lander is on %s/%d\n",lander.get_ip_address(),lander.get_port() );
  printf("dash   is on %s/%d\n",dash.get_ip_address(),dash.get_port() );

  /* start event dispatching thread */
  dispatch.start( callback(&periodic, &EventQueue::dispatch_forever) );

  periodic.call_every(50,communications);
  periodic.call_every(1000, dashboard);
  periodic.call_every(500, checkGameStatus);

  // Assign throttleBoostOn and throttleBoostOff functions to joystickUp
  joystickUp.rise(throttleBoostOn);
  joystickUp.fall(throttleBoostOff);

  while(1) {
    /* update display at whatever rate is possible */
    /*TODO show user information on the LCD */
    /*TODO set LEDs as appropriate to show boolean states */


    wait(1);/*TODO you may want to change this time
    to get a responsive display */
  }
}
