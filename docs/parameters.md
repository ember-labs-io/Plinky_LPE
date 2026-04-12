# Parameters

Parameters are arranged in a grid of rows and columns. The **Param 1** and **Param 2** shift buttons select two layers per row. Each row corresponds to a functional area; each column corresponds to a pad position.

```
              col1      col2    col3      col4        col5      col6
<flags>
AB            scale     offset  depth     freq        shape     warp
XY            scale     offset  depth     freq        shape     warp

delay         send      time    ratio     wobble      feedback  env-level
reverb        send      time    shimmer   wobble                env-repeat

pitch         octave    pitch   glide     interval    gatelen   env-rate
scale         rotate    scale   micro     stride      pwm       env-warp

amp           sens      drive   A         D           S         R
mix           synth     input   inp->fx   wet/dry     hpf       reso

arp           arpmode   div     prob      euclen      oct       tempo
seq           seqmode   div     prob      euclen      pat       step

sampler       sample    pos     rate      grainsz     time      headphone
              noise     posj    ratej     grainszj              cv quant
```
