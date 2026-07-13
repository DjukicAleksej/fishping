export const BLE_UUIDS = Object.freeze({
  service: 'f15bb8d0-7a6f-4f3c-9a91-0fd76a6f1000',
  stats: 'f15bb8d0-7a6f-4f3c-9a91-0fd76a6f1001',
  event: 'f15bb8d0-7a6f-4f3c-9a91-0fd76a6f1002',
  command: 'f15bb8d0-7a6f-4f3c-9a91-0fd76a6f1003',
});

const textDecoder = new TextDecoder();
const textEncoder = new TextEncoder();

const clamp = (value, min, max) => Math.min(max, Math.max(min, value));

function asNumber(value, fallback = 0) {
  const number = Number(value);
  return Number.isFinite(number) ? number : fallback;
}

function decodeText(payload) {
  if (payload instanceof DataView) {
    return textDecoder.decode(new Uint8Array(payload.buffer, payload.byteOffset, payload.byteLength));
  }

  if (payload instanceof Uint8Array) {
    return textDecoder.decode(payload);
  }

  return String(payload);
}

export function decodeFishPingPayload(payload) {
  const raw = JSON.parse(decodeText(payload));

  return {
    biteCount: asNumber(raw.c),
    eventId: asNumber(raw.e),
    lastBiteMs: asNumber(raw.last),
    sinceLastBiteMs: raw.since === undefined ? null : asNumber(raw.since),
    lastIntervalMs: asNumber(raw.interval),
    armed: raw.armed === 1 || raw.armed === true || raw.armed === '1',
    sensitivity: asNumber(raw.sens, 6),
    cooldownMs: asNumber(raw.cool, 2500),
    batteryMv: asNumber(raw.v, -1),
    magnitudeG: asNumber(raw.m),
    jerkG: asNumber(raw.j),
    sensorAddress: asNumber(raw.addr),
    sensorError: raw.err === 1 || raw.err === true || raw.err === '1',
  };
}

export function formatDuration(ms) {
  if (ms === null || ms === undefined || !Number.isFinite(Number(ms))) {
    return '--';
  }

  const totalSeconds = Math.max(0, Math.floor(Number(ms) / 1000));

  if (totalSeconds < 60) {
    return `${totalSeconds}s`;
  }

  const minutes = Math.floor(totalSeconds / 60);
  const seconds = totalSeconds % 60;

  if (minutes < 60) {
    return `${minutes}m ${String(seconds).padStart(2, '0')}s`;
  }

  const hours = Math.floor(minutes / 60);
  const remainingMinutes = minutes % 60;
  return `${hours}h ${String(remainingMinutes).padStart(2, '0')}m`;
}

export function commandForSensitivity(value) {
  return `SENS=${clamp(Math.round(Number(value) || 1), 1, 10)}`;
}

export function commandForCooldown(value) {
  return `COOLDOWN=${clamp(Math.round(Number(value) || 500), 500, 30000)}`;
}

export function batteryPercentFromMv(mv) {
  const voltage = Number(mv);
  if (!Number.isFinite(voltage) || voltage <= 0) {
    return null;
  }

  return clamp(Math.round(((voltage - 3300) / 900) * 100), 0, 100);
}

export class FishPingModel {
  constructor() {
    this.lastEventId = null;
    this.seenIntervals = new Set();
    this.state = {
      biteCount: 0,
      eventId: 0,
      lastBiteMs: 0,
      sinceLastBiteMs: null,
      lastIntervalMs: 0,
      intervalsMs: [],
      armed: true,
      sensitivity: 6,
      cooldownMs: 2500,
      batteryMv: -1,
      magnitudeG: 0,
      jerkG: 0,
      sensorError: false,
      connected: false,
    };
  }

  applyStats(stats) {
    const normalized = {
      ...this.state,
      ...stats,
      intervalsMs: [...this.state.intervalsMs],
    };

    let biteDetected = false;

    if (this.lastEventId === null) {
      this.lastEventId = normalized.eventId;
    } else if (normalized.eventId !== this.lastEventId) {
      biteDetected = normalized.eventId !== 0;
      this.lastEventId = normalized.eventId;
    }

    if (biteDetected && normalized.lastIntervalMs > 0) {
      const key = `${normalized.eventId}:${normalized.lastIntervalMs}`;
      if (!this.seenIntervals.has(key)) {
        this.seenIntervals.add(key);
        normalized.intervalsMs = [normalized.lastIntervalMs, ...normalized.intervalsMs].slice(0, 6);
      }
    }

    this.state = normalized;
    return { biteDetected, state: this.state };
  }

  setConnected(connected) {
    this.state = { ...this.state, connected };
  }
}

function getElements() {
  return {
    alarmToggle: document.querySelector('#alarmToggle'),
    armButton: document.querySelector('#armButton'),
    battery: document.querySelector('#battery'),
    biteCount: document.querySelector('#biteCount'),
    calibrateButton: document.querySelector('#calibrateButton'),
    connectButton: document.querySelector('#connectButton'),
    connectionDot: document.querySelector('#connectionDot'),
    connectionText: document.querySelector('#connectionText'),
    cooldown: document.querySelector('#cooldown'),
    cooldownValue: document.querySelector('#cooldownValue'),
    deviceName: document.querySelector('#deviceName'),
    eventId: document.querySelector('#eventId'),
    intervalList: document.querySelector('#intervalList'),
    jerk: document.querySelector('#jerk'),
    lastBite: document.querySelector('#lastBite'),
    magnitude: document.querySelector('#magnitude'),
    resetButton: document.querySelector('#resetButton'),
    sensitivity: document.querySelector('#sensitivity'),
    sensitivityValue: document.querySelector('#sensitivityValue'),
    sensorStatus: document.querySelector('#sensorStatus'),
    sinceLast: document.querySelector('#sinceLast'),
    statusPanel: document.querySelector('#statusPanel'),
    testAlarmButton: document.querySelector('#testAlarmButton'),
  };
}

function initApp() {
  const elements = getElements();
  const model = new FishPingModel();

  let bluetoothDevice = null;
  let statsCharacteristic = null;
  let commandCharacteristic = null;
  let audioContext = null;

  const setConnection = (connected, label = connected ? 'Connected' : 'Disconnected') => {
    model.setConnected(connected);
    elements.connectionDot.classList.toggle('is-connected', connected);
    elements.connectionText.textContent = label;
    elements.connectButton.textContent = connected ? 'Reconnect' : 'Connect';
  };

  const render = () => {
    const state = model.state;
    const batteryPercent = batteryPercentFromMv(state.batteryMv);

    elements.biteCount.textContent = String(state.biteCount);
    elements.eventId.textContent = String(state.eventId);
    elements.lastBite.textContent = state.lastBiteMs > 0 ? `${formatDuration(state.lastBiteMs)} after start` : '--';
    elements.sinceLast.textContent = state.lastBiteMs > 0 ? formatDuration(state.sinceLastBiteMs) : '--';
    elements.battery.textContent =
      batteryPercent === null ? '--' : `${batteryPercent}% (${Math.round(state.batteryMv)} mV)`;
    elements.magnitude.textContent = `${state.magnitudeG.toFixed(2)} g`;
    elements.jerk.textContent = `${state.jerkG.toFixed(2)} g`;
    elements.sensitivity.value = String(state.sensitivity);
    elements.sensitivityValue.textContent = String(state.sensitivity);
    elements.cooldown.value = String(state.cooldownMs);
    elements.cooldownValue.textContent = `${state.cooldownMs} ms`;
    elements.armButton.textContent = state.armed ? 'Disarm' : 'Arm';
    elements.sensorStatus.textContent = state.sensorError
      ? 'Sensor not detected'
      : state.sensorAddress > 0
        ? `LIS3DH 0x${state.sensorAddress.toString(16)}`
        : '--';

    elements.intervalList.innerHTML = '';
    if (state.intervalsMs.length === 0) {
      const empty = document.createElement('li');
      empty.textContent = 'No intervals yet';
      elements.intervalList.append(empty);
    } else {
      for (const interval of state.intervalsMs) {
        const item = document.createElement('li');
        item.textContent = formatDuration(interval);
        elements.intervalList.append(item);
      }
    }
  };

  const ensureAudio = async () => {
    if (!audioContext) {
      audioContext = new AudioContext();
    }

    if (audioContext.state === 'suspended') {
      await audioContext.resume();
    }
  };

  const ringAlarm = async () => {
    if (!elements.alarmToggle.checked) {
      return;
    }

    await ensureAudio();

    const now = audioContext.currentTime;
    const pattern = [
      [0.00, 0.16, 880],
      [0.22, 0.18, 1175],
      [0.48, 0.30, 988],
    ];

    for (const [offset, duration, frequency] of pattern) {
      const oscillator = audioContext.createOscillator();
      const gain = audioContext.createGain();
      oscillator.type = 'square';
      oscillator.frequency.value = frequency;
      gain.gain.setValueAtTime(0.001, now + offset);
      gain.gain.exponentialRampToValueAtTime(0.28, now + offset + 0.02);
      gain.gain.exponentialRampToValueAtTime(0.001, now + offset + duration);
      oscillator.connect(gain).connect(audioContext.destination);
      oscillator.start(now + offset);
      oscillator.stop(now + offset + duration + 0.02);
    }

    if ('vibrate' in navigator) {
      navigator.vibrate([240, 80, 240, 80, 420]);
    }

    elements.statusPanel.classList.add('is-alerting');
    window.setTimeout(() => elements.statusPanel.classList.remove('is-alerting'), 1100);
  };

  const writeCommand = async (command) => {
    if (!commandCharacteristic) {
      throw new Error('Connect to FishPing first.');
    }

    await commandCharacteristic.writeValue(textEncoder.encode(command));
  };

  const handleStatsPayload = async (payload) => {
    const stats = decodeFishPingPayload(payload);
    const result = model.applyStats(stats);
    render();

    if (result.biteDetected) {
      await ringAlarm();
    }
  };

  const handleStatsNotification = (event) => {
    handleStatsPayload(event.target.value).catch((error) => {
      elements.connectionText.textContent = error.message;
    });
  };

  const connect = async () => {
    if (!('bluetooth' in navigator)) {
      setConnection(false, 'Web Bluetooth unavailable');
      return;
    }

    await ensureAudio();
    setConnection(false, 'Choosing device...');

    bluetoothDevice = await navigator.bluetooth.requestDevice({
      filters: [{ namePrefix: 'FishPing' }],
      optionalServices: [BLE_UUIDS.service],
    });

    elements.deviceName.textContent = bluetoothDevice.name || 'FishPing Float';
    bluetoothDevice.addEventListener('gattserverdisconnected', () => {
      statsCharacteristic = null;
      commandCharacteristic = null;
      setConnection(false, 'Disconnected');
    });

    setConnection(false, 'Connecting...');
    const server = await bluetoothDevice.gatt.connect();
    const service = await server.getPrimaryService(BLE_UUIDS.service);
    statsCharacteristic = await service.getCharacteristic(BLE_UUIDS.stats);
    commandCharacteristic = await service.getCharacteristic(BLE_UUIDS.command);

    statsCharacteristic.addEventListener('characteristicvaluechanged', handleStatsNotification);
    await statsCharacteristic.startNotifications();

    try {
      const initialValue = await statsCharacteristic.readValue();
      await handleStatsPayload(initialValue);
    } catch (error) {
      elements.connectionText.textContent = error.message;
    }

    setConnection(true);
    render();
  };

  elements.connectButton.addEventListener('click', () => {
    connect().catch((error) => setConnection(false, error.message));
  });

  elements.testAlarmButton.addEventListener('click', () => {
    ringAlarm().catch((error) => {
      elements.connectionText.textContent = error.message;
    });
  });

  elements.resetButton.addEventListener('click', () => {
    writeCommand('RESET').catch((error) => {
      elements.connectionText.textContent = error.message;
    });
  });

  elements.calibrateButton.addEventListener('click', () => {
    writeCommand('CAL').catch((error) => {
      elements.connectionText.textContent = error.message;
    });
  });

  elements.armButton.addEventListener('click', () => {
    const command = model.state.armed ? 'DISARM' : 'ARM';
    writeCommand(command).catch((error) => {
      elements.connectionText.textContent = error.message;
    });
  });

  elements.sensitivity.addEventListener('input', () => {
    elements.sensitivityValue.textContent = elements.sensitivity.value;
  });

  elements.sensitivity.addEventListener('change', () => {
    writeCommand(commandForSensitivity(elements.sensitivity.value)).catch((error) => {
      elements.connectionText.textContent = error.message;
    });
  });

  elements.cooldown.addEventListener('input', () => {
    elements.cooldownValue.textContent = `${elements.cooldown.value} ms`;
  });

  elements.cooldown.addEventListener('change', () => {
    writeCommand(commandForCooldown(elements.cooldown.value)).catch((error) => {
      elements.connectionText.textContent = error.message;
    });
  });

  if (!window.isSecureContext) {
    setConnection(false, 'Use HTTPS or localhost');
  } else if (!('bluetooth' in navigator)) {
    setConnection(false, 'Use Android Chrome');
  }

  if ('serviceWorker' in navigator) {
    navigator.serviceWorker.register('./sw.js').catch(() => {});
  }

  render();
}

if (typeof window !== 'undefined') {
  window.addEventListener('DOMContentLoaded', initApp);
}
