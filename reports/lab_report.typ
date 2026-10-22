#set page(paper: "a4", margin: (x: 1.5cm, y: 1.3cm))
#set text(size: 8.6pt, hyphenate: false, font: "DejaVu Sans")
#set par(justify: true, leading: 0.48em, spacing: 0.62em)
#set heading(numbering: "1.1")
#show heading.where(level: 1): set text(size: 10pt, rgb("#4e7aa1"))
#show heading.where(level: 1): set block(above: 0.75em, below: 0.75em)
#show heading.where(level: 2): set text(size: 8.8pt, rgb("#4e7aa1"))
#show heading.where(level: 2): set block(above: 0.75em, below: 0.75em)
#show figure.caption: set text(size: 7.3pt)
#show figure.where(kind: table): set figure.caption(position: top)
#set figure(gap: 0.35em)

#align(center)[
  #text(size: 13pt, weight: "bold")[Lab Report: Sampling and Reconstruction of a Sinusoid with FFT Analysis]
]
#v(0.2em)
#grid(
  columns: (1fr, 1fr),
  row-gutter: 0.3em,
  [*Course Code & Name:* PH 3039 : Data Acquisition],
  [*Student Name:* M.W.T.S.Sampath],
  [*Student ID:* s17278],
  [*Lab Partner(s):* Kenura Gunarathna, Kaveesha],
)

#heading(numbering: none, level: 1)[Abstract]
This experiment investigates how accurately an analogue sinusoid can be recovered from its samples. A Seeed Studio XIAO nRF52840 Sense sampled the output of a Keysight EDU33212A waveform generator through its 12 bit ADC at a constant rate, while a Python script on the PC controlled the generator, stepped the frequency across 10 to 1000 Hz and logged the data automatically. The generator output was limited to the 0 to 3 V input range of the ADC, so no protection circuit was needed. Each record was reconstructed in Python and assessed in the time domain with a least squares sine fit and in the frequency domain with the FFT. Sweeps with different frequency steps were compared to see whether the step size affects the analysis, and aliasing was demonstrated by deliberately lowering the sampling rate of a recorded signal. The work covers the Nyquist criterion, quantisation, spectral resolution and the practical limits of the measurement chain.

= Introduction & Theoretical Background
A digital system sees an analogue signal only through its samples, so the sampling rate decides what can be recovered. The Nyquist theorem states that a band limited signal with highest frequency $f_"max"$ is fully described by samples taken at a rate above $2 f_"max"$. Below that rate, components fold back to false lower frequencies. This is aliasing, and it cannot be undone after sampling, so an antialiasing filter ahead of the ADC is the preventive measure. Two further effects set the accuracy. Quantisation with step $q$ adds noise of rms $q slash sqrt(12)$, and the finite record length $N$ limits the FFT resolution to $Delta f = f_s slash N$. The goals were to sample sine waves from 10 Hz to 1 kHz at a constant rate, reconstruct them, judge the quality in the time and frequency domains, check whether the sweep step changes the result, and verify the aliasing prediction.

= Experimental Setup & Procedure
== Apparatus & Materials
The source was a Keysight EDU33212A generator connected to the PC, so frequency and amplitude were set by script. Its output went to pin A0 of the XIAO nRF52840 Sense with the grounds of both devices joined. The lab sheet names the RP2040, but the XIAO board was used here. The output was fixed at 3 V peak to peak with a 1.5 V offset, which keeps the signal between 0 and 3 V, so no snubber was used. The ADC is 12 bit with a 3.6 V full scale (gain 1/6), giving a step $q = 3.6 slash 4095 = 0.879$ mV. The firmware `sampling.cpp` was built and uploaded with Arduino CLI and samples at a fixed 20 kHz. Each CSV row holds `freq_hz` and `gen_ref_v` from the generator, and `time_ms` and `adc_v` from the microcontroller. Analysis used Python with pandas, NumPy, SciPy and Matplotlib.

== Experimental Procedure
For each commanded frequency the script set the generator, recorded about 30 cycles (limited to 50000 samples at 10 Hz) and stored the rows. The sweep over 10 to 1000 Hz was repeated with steps of 1, 5, 10, 20 and 50 Hz. The 1 Hz sweep alone produced 2.78 million rows, which made analysis slow, so the 10 Hz and 50 Hz sweeps (301205 and 89230 rows) were analysed to see whether the step matters.

= Results & Data Analysis
== Data Tables
#grid(
  columns: (1.12fr, 1fr),
  column-gutter: 0.5cm,
  [
    #figure(
      text(size: 7.3pt)[
        #table(
          columns: 7,
          stroke: 0.4pt,
          inset: 2.2pt,
          align: center,
          [*f (Hz)*], [*N*], [*Bin (Hz)*], [*Peak (Hz)*], [*A (V)*], [*$R^2$*], [*SNR (dB)*],
          [10], [50000], [0.40], [9.9999], [1.5022], [0.9923], [45.5],
          [110], [5454], [3.67], [110.0021], [1.5020], [0.9922], [44.7],
          [210], [2857], [7.00], [210.0020], [1.5020], [0.9922], [47.3],
          [310], [1935], [10.34], [310.0190], [1.5019], [0.9922], [46.9],
          [410], [1463], [13.67], [410.0244], [1.5022], [0.9922], [46.6],
          [510], [1176], [17.01], [510.0503], [1.5013], [0.9922], [47.0],
          [610], [983], [20.35], [610.0883], [1.5009], [0.9919], [39.6],
          [710], [845], [23.67], [710.0119], [1.5022], [0.9924], [48.0],
          [810], [740], [27.03], [810.2147], [1.5014], [0.9923], [46.5],
          [910], [659], [30.35], [910.1116], [1.5018], [0.9924], [48.1],
          [960], [625], [32.00], [960.0008], [1.5023], [0.9923], [48.8],
        )
      ],
      caption: [Selected records from the 50 Hz step sweep. A is the fitted amplitude (commanded 1.5 V).],
    ) <tab1>
  ],
  [
    #figure(
      text(size: 7.3pt)[
        #table(
          columns: (2.1fr, 1fr, 1fr),
          stroke: 0.4pt,
          inset: 2.2pt,
          align: (left, center, center),
          [*Metric*], [*10 Hz step*], [*50 Hz step*],
          [Frequencies], [100], [20],
          [Rows logged], [301205], [89230],
          [Mean $R^2$], [0.9921], [0.9922],
          [Median SNR (dB)], [45.7], [46.9],
          [Median noise floor (mV)], [5.2], [4.6],
          [Records with floor above 10 mV], [16 of 100], [2 of 20],
          [Mean amplitude error (%)], [0.146], [0.109],
          [Median peak error (Hz)], [0.027], [0.022],
          [Largest peak error (Hz)], [0.264], [0.215],
        )
      ],
      caption: [Comparison of the two analysed sweeps.],
    ) <tab2>
  ],
)

== Model Fitting & Reconstruction Quality
Each record was fitted by least squares with $y(t) = C + M sin(2 pi f t) + N cos(2 pi f t)$, giving the amplitude $A = sqrt(M^2 + N^2)$ and the phase $phi$. A sample by sample comparison with `gen_ref_v` gave negative $R^2$ at most frequencies, because the generator and the ADC clocks are not synchronised and the phase between them is arbitrary. The reference was therefore rebuilt as $1.5 + 1.5 sin(2 pi f t + phi)$ with the fitted phase, so the residual holds only amplitude, offset and noise errors. The noise floor is the residual about the fit itself. For the spectrum a Hann window was applied and the peak frequency and amplitude were refined by parabolic interpolation. The sample interval was 0.0500 ms with zero standard deviation over all records, confirming a constant 20 kHz rate.

#figure(
  image("figures/fig_residuals.png", width: 88%),
  caption: [Residual against the phase aligned reference (red) and noise floor about the fit (green) at 10, 510 and 1000 Hz, 10 Hz step sweep. Dotted lines mark the quantisation noise.],
) <fig1>

#grid(
  columns: (1.05fr, 1fr),
  column-gutter: 0.5cm,
  [
    #figure(
      image("figures/fig_errors.png", width: 100%),
      caption: [Peak frequency error (bars show half an FFT bin) and amplitude error against the commanded 1.5 V, 10 Hz step sweep.],
    ) <fig2>
  ],
  [
    To demonstrate aliasing, the 1000 Hz record was decimated by factors 12, 20 and 25. The predicted alias is the distance from $f$ to the nearest multiple of $f_s'$. At $f_s' = 1000$ Hz the tone folds to DC and disappears when the mean is removed.
    #v(0.3em)
    #figure(
      text(size: 7.3pt)[
        #table(
          columns: 4,
          stroke: 0.4pt,
          inset: 2.2pt,
          align: center,
          [*$f_s'$ (Hz)*], [*Nyquist (Hz)*], [*Predicted (Hz)*], [*Measured (Hz)*],
          [20000], [10000], [1000.0], [1000.0],
          [1666.7], [833.3], [666.7], [666.7],
          [800.0], [400.0], [200.0], [200.0],
        )
      ],
      caption: [Aliasing of the 1000 Hz tone after decimation.],
    ) <tab3>
  ],
)

= Error & Uncertainty Analysis
== Sources of Error
*Quantisation.* The step is 0.879 mV and the rms noise 0.254 mV, about 20 times below the median noise floor of 5 mV, so quantisation does not limit the measurement. *Noise and interference.* A few records show floors of 10 to 27 mV, and @fig1 shows clustered spikes reaching about 100 mV. Their cause was not identified. *DC level mismatch.* The phase aligned RMSE is about 0.094 V at every frequency, far above the noise floor, while the fitted amplitude stays within 0.4% of 1.5 V. This implies a constant difference of about 0.094 V, with the ADC reading below the commanded offset (the residual in @fig1 is centred near 90 mV below zero). The data do not show whether it comes from the ADC or the generator. This offset alone caps $R^2$ near 0.992. *Resolution and timing.* The bin width is $f slash 30$, from 0.4 Hz to 33 Hz. After interpolation the peak error is mostly positive above 300 Hz and stays below 0.04%. Interpolation bias and a small clock mismatch between the two devices could not be separated.

== Uncertainty Calculations
The standard error of the fitted amplitude is $sigma_A approx sigma_n sqrt(2 slash N)$, with $sigma_n$ the noise floor. At 10 Hz in the 50 Hz sweep, $N = 50000$ and $sigma_n = 5.44$ mV give $sigma_A = 0.034$ mV, and the mean over the 10 Hz sweep is 0.27 mV. The observed mean amplitude error is 0.146% of 1.5 V, which is 2.2 mV, about eight times larger, so the amplitude error is systematic rather than statistical. Relative to the commanded 1.5 V the amplitude error ranges from 0.30% below to 0.39% above. Since the phase aligned RMSE satisfies $"RMSE"^2 = "offset"^2 + sigma_n^2$, the offset of 0.094 V dominates the uncertainty of a single reading and the random part is only about 5 mV.

= Discussion & Conclusion
At 20 kHz the 1 kHz tone has 20 samples per cycle, ten times the Nyquist minimum, and the reconstruction is correspondingly clean: frequency is recovered to within 0.04% and amplitude to within 0.4% at every tested frequency. The sweep step did not change the quality (@tab2). Every frequency is recorded independently, so the step changes only the coverage. The 10 Hz step is the better choice because it reaches 1000 Hz, while the 1 Hz sweep only adds data volume. The decimation results agree with this prediction (@tab3), which shows why an antialiasing filter is needed whenever the sampling rate is below twice the highest signal frequency. The main remaining limitation is the constant offset, and measuring it directly with a multimeter would show whether the ADC or the generator is responsible. The final reconstruction model is $y(t) = C + A sin(2 pi f t + phi)$ with $A = 1.502$ V against 1.5 V commanded, valid from 10 to 1000 Hz at $f_s = 20$ kHz with a typical SNR of 46 dB.
