<template>
  <v-container>
    <v-row>
      <v-col cols="6">
        <v-card>
          <v-card-title>UART</v-card-title>
          <v-card-text>
            <v-btn @click="connect" :disabled="connected">
              Connect
            </v-btn>
          </v-card-text>
        </v-card>
        <br />
        <v-card>
          <v-card-title>Program</v-card-title>
          <v-card-text>
            <v-text-field
              label="Step (mm)"
              v-model="step"
              :rules="stepRules"
            ></v-text-field>
            <v-text-field
                label="Count"
                v-model="count"
                :rules="countRules"
              ></v-text-field>
            <v-text-field
              label="Post-shutter Delay (s)"
              v-model="postShutterDelay"
              :rules="delayRules"
            ></v-text-field>
            <span>total distance: {{ totalDistance }}</span> mm, total time: <span>{{ totalTime }}</span>s
            <br><br>
            <v-btn
              color="primary"
              elevation="2"
              x-large
              :disabled="running"
              @click="sendProgram"
            >Start
            </v-btn>&nbsp;
            <v-btn
              color="primary"
              elevation="2"
              x-large
              :disabled="!running"
              @click="sendMotionCommand('pause')"
            >Pause/Resume
            </v-btn>&nbsp;
            <v-btn
              color="primary"
              elevation="2"
              x-large
              :disabled="!running"
              @click="sendReset"
            >Reset
            </v-btn>
          </v-card-text>
        </v-card>
        <br>
        <v-card>
          <v-card-title>Manual Control</v-card-title>
          <v-card-text>
            <v-btn
              color="primary"
              elevation="2"
              x-large
              @mousedown="sendMotionCommand('left')"
              @mouseup="sendMotionCommand('stop')"
              ><v-icon>mdi-arrow-left</v-icon>
            </v-btn>&nbsp;
            <v-btn
              color="primary"
              elevation="2"
              x-large
              @mousedown="sendMotionCommand('right')"
              @mouseup="sendMotionCommand('stop')"
              ><v-icon>mdi-arrow-right</v-icon>
            </v-btn>&nbsp;
            <v-btn
              color="primary"
              elevation="2"
              x-large
              @click="sendMotionCommand('shutter')"
              >Shutter
            </v-btn>
            <br><br>
            position: <span>{{ position }}</span> mm<br>
            motor current: <span>{{ current }}</span> mA
          </v-card-text>
        </v-card>
      </v-col>
    </v-row>
  </v-container>
</template>

<script>
import serial from "../serial";

export default {
  name: "Main",
  data() {
    return {
      speed: "1",
      count: "10",
      step: "1",
      preShutterDelay: "0.2",
      postShutterDelay: "0.5",
      delayRules: [
        (v) => !!v || "This field is required",
        (v) => (v && v >= 0.1) || "Minimum 0.1",
        (v) => (v && v <= 100) || "Maximum 100",
      ],
      countRules: [
        (v) => !!v || "This field is required",
        (v) => (v && v >= 0) || "Minimum 0",
        (v) => (v && v <= 255) || "Maximum 255 steps",
        (v) => (v && v * this.step <= 80) || "Maximum distance reached",
      ],
      stepRules: [
        (v) => !!v || "This field is required",
        (v) => (v && v >= 0) || "Minimum 0.0",
        (v) => (v && v * this.count <= 80) || "Maximum distance reached",
      ],
      connected: false,
      running: false,
      position: 0,
      current: 0,
    };
  },
  computed: {
    totalDistance: function() {
      return Math.round(Number(this.count) * Number(this.step));
    },
    totalTime: function() {
      return Math.round(Number(this.count) * (Number(this.preShutterDelay) + Number(this.postShutterDelay)));
    }
  },
  methods: {
    async connect() {
      await serial.connect(115200);
      this.connected = true;
      serial.addCallback(serial.messageTypes.position, this.updatePosition);
      serial.addCallback(serial.messageTypes.current, this.updateCurrent);
    },
    updatePosition(value) {
      this.position = value / 1000;
    },
    updateCurrent(value) {
      this.current = value;
    },
    sendMotionCommand(command) {
      serial.sendCommand(command);
    },
    sendProgram() {
      this.running = true;
      serial.sendProgram(this.step, this.postShutterDelay, this.count);
    },
    sendReset() {
      this.running = false;
      serial.sendCommand('reset');
    }
  },
  components: {
  },
};
</script>
