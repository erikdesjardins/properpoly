/*
 * PARAMETERS:
 *
 * PMUXCH = input MIDI play channel of the polyMUX
 * PMUXBASECH = output base MIDI channel of polyMUX
 * PMUXPOLY: 1-8, number of polyMUX polyphony channels
 *
 * STARTUP CONFIGURATION:
 *
 * e.g. Press a key on keystep, plug in RK002 and release within three seconds.
 * PMUXCH will be set to current Keystep MIDI channel.
 * PMUXPOLY will be set based on the note (C = 1, C# = 2, etc.)
 */

#define APP_NAME "Syntakt PolyMUX (long note compatible)"
#define APP_AUTHOR "erikdesjardins"
#define APP_VERSION "0.3"
#define APP_GUID "783b9c09-9bf5-4ea8-8faf-f59e668768c1"

RK002_DECLARE_INFO(APP_NAME, APP_AUTHOR, APP_VERSION, APP_GUID)

RK002_DECLARE_PARAM(PMUXCH, 1, 1, 16, 15)
RK002_DECLARE_PARAM(PMUXBASECH, 1, 1, 16, 1)
RK002_DECLARE_PARAM(PMUXPOLY, 1, 1, 12, 4)

byte inChannel = 0;
byte outBaseChannel = 0;
byte polyphony = 0;

byte heartCount = 150;

void updateParams()
{
    // input channel (1-16 -> 0-15 internally)
    inChannel = (RK002_paramGet(PMUXCH) == 0) ? 16 : RK002_paramGet(PMUXCH) - 1;
    // base channel (1-16 -> 0-15 internally)
    outBaseChannel = (RK002_paramGet(PMUXBASECH) == 0) ? 0 : RK002_paramGet(PMUXBASECH) - 1;
    // polyphony
    polyphony = RK002_paramGet(PMUXPOLY);
}

void trainKey(byte channel, byte note)
{
    // train note -> set polyphony
    note = note % 12;
    byte poly = min(12, (note + 1));
    RK002_paramSet(PMUXPOLY, poly);

    // train index -> set polymux input channel
    RK002_paramSet(PMUXCH, channel + 1);
    heartCount = 0;
    updateParams();
}

void RK002_onParamChange(unsigned param_nr, int param_val)
{
    updateParams();
}

// called every 100ms
void RK002_onHeartBeat()
{
    if (heartCount)
        heartCount--;
}

// Active key per polymux channel.
// 0xff = no active key
byte activekey[12] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

unsigned int epoch = 0;

// Epoch timestamp when key was pressed.
// Used to decide which key to evict when polyphony is full.
unsigned int activeepoch[12] = {0};

void sendPolyMuxOutput(byte polymuxidx, byte cmd, byte d1, byte d2)
{
    byte chn = outBaseChannel + polymuxidx;

    if (chn <= 15)
    {
        byte sts = cmd | chn;
        RK002_sendChannelMessage(sts, d1, d2);
    }
}

void inputToPolyMux(byte cmd, byte d1, byte d2)
{
    switch (cmd)
    {
    case 0x90: // note on
    {
        if (d2 > 0)
        {
            // See if this key is already pressed.
            // This shouldn't be possible unless you're feeding in input from more than one source,
            // and it means we lose the ability to track key releases, so overwrite the existing note if one exists.
            for (int idx = 0; idx < polyphony; ++idx)
            {
                if (activekey[idx] == d1)
                {
                    sendPolyMuxOutput(idx, 0x90, d1, d2);
                    return;
                }
            }

            // Now that we know this is a new key, try to find the first unused channel.
            for (int idx = 0; idx < polyphony; ++idx)
            {
                if (activekey[idx] == 0xff)
                {
                    activekey[idx] = d1;
                    activeepoch[idx] = epoch++;
                    sendPolyMuxOutput(idx, 0x90, d1, d2);
                    return;
                }
            }

            // There are no unused channels, so evict the oldest key.
            int oldestidx = 0;
            unsigned int oldestage = 0;
            for (int idx = 0; idx < polyphony; ++idx)
            {
                unsigned int age = epoch - activeepoch[idx];
                if (age > oldestage)
                {
                    oldestidx = idx;
                    oldestage = age;
                }
            }
            // Send note off for existing key...
            sendPolyMuxOutput(oldestidx, 0x80, activekey[oldestidx], 0);
            // ...and replace it with the new key.
            activekey[oldestidx] = d1;
            activeepoch[oldestidx] = epoch++;
            sendPolyMuxOutput(oldestidx, 0x90, d1, d2);
            return;
        }
        else
        {
            // velo == 0 -> note off
            // fallthrough
        }
    }
    case 0x80: // note off
    {
        for (int idx = 0; idx < polyphony; ++idx)
        {
            if (activekey[idx] == d1)
            {
                sendPolyMuxOutput(idx, 0x80, d1, d2);
                activekey[idx] = 0xff;
                return;
            }
        }
        break;
    }
    case 0xA0: // aftertouch
    {
        for (int idx = 0; idx < polyphony; ++idx)
        {
            if (activekey[idx] == d1)
            {
                // transform poly aftertouch to mono aftertouch (channel pressure)
                sendPolyMuxOutput(idx, 0xD0, d2, 0);
                return;
            }
        }
        break;
    }
    default: // anything else (pitch bend, etc.)
    {
        // send to all possible channels
        for (byte idx = 0; idx < polyphony; ++idx)
        {
            sendPolyMuxOutput(idx, cmd, d1, d2);
        }
        break;
    }
    }
}

bool RK002_onChannelMessage(byte sts, byte d1, byte d2)
{
    byte cmd = sts & 0xf0;
    byte chn = sts & 0x0f;

    // Handle training on startup
    switch (cmd)
    {
    case 0x90: // note on
        if (d2 > 0)
        {
            if (heartCount)
            {
                heartCount = 0; // key was not held before startup, cancel training
            }
            break;
        }
        else
        {
            // velo == 0 -> note off
            // fallthrough
        }
    case 0x80: // note off
        if (heartCount)
        {
            heartCount = 0; // disable training after this
            trainKey(chn, d1);
        }
        break;
    }

    bool ischannelmessage = sts >= 0x80 && sts <= 0xEF;

    if (ischannelmessage && chn == inChannel)
    {
        inputToPolyMux(cmd, d1, d2);

        return false; // don't send through
    }
    else
    {
        return true; // send through
    }
}

void setup()
{
    updateParams();
}

void loop() {
    // do nothing
}
