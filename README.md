
# Homework
The goal of the assignment is to create an IoT system that collects information from a sensor, analyses the data locally and communicates to a nearby server an aggregated value of the sensor readings. The IoT system adapts the sampling frequency in order to save energy and reduce communication overhead. The IoT device will be based on an ESP32 prototype board and the firmware will be developed using the FreeRTOS. You are free to use IoT-Lab or real devices.

Input: Assume an input signal of the form of SUM(a_k*sin(f_k)). 
For example: 2*sin(2*pi*3*t)+4*sin(2*pi*5*t)

Maximum sampling frequency: Identify the maximum sampling frequency of your hardware device, for example, 100Hz. Note 100Hz is only an example. You need to demonstrate the ability of sampling at a very high frequency.

Identify optimal sampling frequency: Compute the FFT and adapt the sampling frequency accordingly. For example, for a maximum frequency of 5 Hz adapt the  sampling frequency to 10Hz.

Compute aggregate function over a window: Compute the average of the sampled signal over a window, for example, 5 secs.

Communicate the aggregate value to the nearby server: Transmit the aggregate value, i.e. the average, to a nearby edge server using MQTT over WIFI.

Communicate the aggregate value to the cloud: Transmit the aggregate value, i.e. the average, to a cloud server using LoRaWAN + TTN.

Measure the performance of the system:
Evaluate the savings in energy of the new/adaptive sampling frequency against the original over-sampled one. Note that in some cases, the optimized sampling frequency cannot be employed due to the latencies of sleeping policies.
Measure per-window execution time.
Measure the volume of data transmitted over the network when the new/adaptive sampling frequency is used against the original over sampled one.
Measure the end-to-end latency of the system. From the point the data are generated up to the point they are received from the edge server.
Use an LLM of your choice:
Implement the aforementioned service through a series of prompts. Comment on the quality of the code produced. Provide the series of prompt issued to achieve the desired outcome.
Comment on the opportunities and limitations of the LLM.
Bonus 
Consider at least 3 different input signals and measure the performance of the system. Discuss different types of an input signal may affect the overall performance in the case of adaptive sampling vs basic/over-sampling.
In addition to the original clean sinusoidal signal, use an alternative noisy signal type that models : s(t) = 2*sin(2*pi*3*t)+4*sin(2*pi*5*t) + n(t) + A(t) where n(t) is Gaussian noise with small sigma (e.g., σ=0.2) modelling sensor baseline noise and A(t) is an anomaly injection component: a sparse random spike process where, with low probability p (e.g. p = 0.02 per sample), a large-magnitude outlier +/- U(5, 15) is injected that models transient hardware faults, EMI interference, or physical disturbances. Introduce an anomaly-aware filter over a given window using (a) Z-score (assumes low contamination of spikes and mainly Gaussian noise) and (b) Hamper (preferable when anomaly injection rate is high). Evaluate the detection performance of each of the two filters using (since anomalies are synthetically injected, their positions are known): True Positive Rate (TPR) - Fraction of injected anomalies correctly flagged; False Positive Rate (FPR) - Fraction of clean samples incorrectly flagged; Mean Error Reduction across different anomaly injection rates (p=1%, 5%, 10%). Measure the execution time and the energy impact of of each filter. Discuss the impact of anomaly spikes on the  in the FFT. Measure and compare the FFT-estimated dominant frequency anomaly-contaminated signal (unfiltered) and the FFT-estimated dominant frequency anomaly pre-filtering. Discuss the resulting adaptive sampling frequency difference and its energy impact with and without filtering. Measure the effect of the window size of the filter on the performance of system in terms of computational effort, end-to-end delay increase, memory usage. Larger windows improve statistical estimates but increase latency and memory use. Characterize this trade-off empirically.
What/How to submit
Create a GitHub repository where you will push all your code and scripts that are need to realize the above assignment.
Within the main README.md file you need to provide all the necessary technical details that address the questions stated above.
The GitHub repository should provide a hands-on walk through of the system, clearly explaining how to set up and run your system.

Collaborations between students and material on the internet.
The above assignment is done by each student individually. Clearly you should discuss with other students of the course about the
assignments. However, you must understand well your solutions, the code and the final write-up must be yours and written in isolation. In
addition, even though you may discuss about how you could implement an algorithm, what type of libraries to use, and so on, the final code must be yours. You may also consult the internet for information, as long as it does not reveal the solution. If a question asks you to design and  implement an algorithm for a problem, it's fine if you find information  about how to resolve a problem with character encoding, for example, but it is not fine if you search for the code or the algorithm for the problem you are being asked. For the projects, you can talk with other students of the course about questions on the programming language, libraries, some API issue, and so on, but both the solutions and the programming must be yours. You are also encouraged to use chatGPT (Claude AI, Gemini, Perplexity, or any
other Large Language Models (LLM) chatbot tool) as allies to help you solve your homework, and hope you could learn how to use them properly. However, using such tools does not imply adopting their solutions as your own without critical thinking. If we find out that you have violated the policy and you have copied in any way you will automatically fail. If you have any doubts about whether something is allowed or not, ask the instructor.


EVALUATION
Evaluation will be performed during a workshop in the class. You have to show running code capable to receive in input a signal as defined above, e.g. consider the virtual sensor discussed during the class, see link)
Correctness - Your code works properly in terms of identifying the max freq of the input signal, computing the aggregate function and transmitting to the edge/cloud.
Performance Evaluation - Evaluate correctly the saving in energy, communication cost and end-to-end latency.
Quality of the free-RTOS code, presentation (discussions/figures) and structure of the GitHub repository.
Bonus

We are free to ask any question about your code and to evaluate your answer to increase or decrease the above points

## Commands
pio run -t upload -e espwroom32

platformio device monitor -b 115200


# results

## sampling
### Energy & Time

1. sampling_sender,label=10k_no_sleep,target_hz=10000,target_samples=50000,signal_hz=10000,period_us=100,elapsed_us=5000006,generator_hz=10402.79,avg_current_mA=62.35,avg_power_mW=291.86,energy_mJ=1459.31,power_samples=833,generator_samples=52014,last_dac=175,missed_deadlines=204
2. sampling_sender,label=10k_light_sleep,target_hz=10000,target_samples=50000,signal_hz=10000,period_us=100,elapsed_us=5000002,generator_hz=10401.80,avg_current_mA=62.19,avg_power_mW=292.53,energy_mJ=1462.66,power_samples=833,generator_samples=52009,last_dac=203,missed_deadlines=205
3. sampling_sender,label=256_no_sleep,target_hz=256,target_samples=1280,signal_hz=10000,period_us=3906,elapsed_us=5003591,generator_hz=10401.73,avg_current_mA=62.31,avg_power_mW=291.32,energy_mJ=1457.67,power_samples=834,generator_samples=52046,last_dac=210,missed_deadlines=5
4. sampling_sender,label=256_light_sleep,target_hz=256,target_samples=1280,signal_hz=10000,period_us=3906,elapsed_us=5003590,generator_hz=10401.93,avg_current_mA=62.37,avg_power_mW=290.74,energy_mJ=1454.72,power_samples=834,generator_samples=52047,last_dac=86,missed_deadlines=5
sampling_sender_run,total_elapsed_us=21945996,total_elapsed_ms=21946.00


sampling_sender,label=10k_no_sleep,target_hz=10000,target_samples=200000,signal_hz=10000,period_us=100,elapsed_us=20000003,generator_hz=10100.70,avg_current_mA=63.77,avg_power_mW=298.68,energy_mJ=5973.68,power_samples=3333,generator_samples=202014,last_dac=175,missed_deadlines=204
sampling_sender,label=10k_light_sleep,target_hz=10000,target_samples=200000,signal_hz=10000,period_us=100,elapsed_us=20000004,generator_hz=10100.45,avg_current_mA=63.75,avg_power_mW=298.37,energy_mJ=5967.35,power_samples=3333,generator_samples=202009,last_dac=203,missed_deadlines=205
sampling_sender,label=256_no_sleep,target_hz=256,target_samples=5120,signal_hz=10000,period_us=3906,elapsed_us=20002632,generator_hz=10100.47,avg_current_mA=63.41,avg_power_mW=296.45,energy_mJ=5929.70,power_samples=3334,generator_samples=202036,last_dac=228,missed_deadlines=5
sampling_sender,label=256_light_sleep,target_hz=256,target_samples=5120,signal_hz=10000,period_us=3906,elapsed_us=20002642,generator_hz=10100.62,avg_current_mA=63.11,avg_power_mW=294.39,energy_mJ=5888.53,power_samples=3333,generator_samples=202039,last_dac=127,missed_deadlines=5
sampling_sender_run,total_elapsed_us=81944016,total_elapsed_ms=81944.02

## FFT & AVG
### Energy & Time

fft_average_sender,label=10k,target_hz=10000,duration_us=102400,expected_samples=1024,elapsed_us=1894,elapsed_ms=1.8940,generated_samples=310,generated_hz=163674.7624,avg_current_mA=29.661828,avg_power_mW=136.900739,energy_mJ=0.259290,last_dac=120
fft_average_sender,label=256,target_hz=256,duration_us=4000000,expected_samples=1024,elapsed_us=2085,elapsed_ms=2.0850,generated_samples=310,generated_hz=148681.0552,avg_current_mA=26.751799,avg_power_mW=124.359712,energy_mJ=0.259290,last_dac=220
FFT average sender completed
fft_average_sender,label=10k,target_hz=10000,duration_us=102400,expected_samples=1024,elapsed_us=2132,elapsed_ms=2.1320,generated_samples=310,generated_hz=145403.3771,avg_current_mA=26.471200,avg_power_mW=123.626642,energy_mJ=0.263572,last_dac=57
fft_average_sender,label=256,target_hz=256,duration_us=4000000,expected_samples=1024,elapsed_us=2324,elapsed_ms=2.3240,generated_samples=330,generated_hz=141996.5577,avg_current_mA=48.098150,avg_power_mW=224.303787,energy_mJ=0.521282,last_dac=188
FFT average sender completed
fft_average_sender,label=10k,target_hz=10000,duration_us=102400,expected_samples=1024,elapsed_us=2104,elapsed_ms=2.1040,generated_samples=310,generated_hz=147338.4030,avg_current_mA=26.817871,avg_power_mW=125.022814,energy_mJ=0.263048,last_dac=165
fft_average_sender,label=256,target_hz=256,duration_us=4000000,expected_samples=1024,elapsed_us=56813,elapsed_ms=56.8130,generated_samples=860,generated_hz=15137.3805,avg_current_mA=55.316282,avg_power_mW=254.430782,energy_mJ=14.454976,last_dac=92
FFT average sender completed
fft_average_sender,label=10k,target_hz=10000,duration_us=102400,expected_samples=1024,elapsed_us=60494,elapsed_ms=60.4940,generated_samples=900,generated_hz=14877.5085,avg_current_mA=55.858675,avg_power_mW=256.643006,energy_mJ=15.525362,last_dac=86
fft_average_sender,label=256,target_hz=256,duration_us=4000000,expected_samples=1024,elapsed_us=2283,elapsed_ms=2.2830,generated_samples=310,generated_hz=135786.2462,avg_current_mA=49.331932,avg_power_mW=231.740692,energy_mJ=0.529064,last_dac=82
FFT average sender completed
fft_average_sender,label=10k,target_hz=10000,duration_us=102400,expected_samples=1024,elapsed_us=2538,elapsed_ms=2.5380,generated_samples=320,generated_hz=126083.5303,avg_current_mA=43.804885,avg_power_mW=205.400315,energy_mJ=0.521306,last_dac=148
fft_average_sender,label=256,target_hz=256,duration_us=4000000,expected_samples=1024,elapsed_us=63495,elapsed_ms=63.4950,generated_samples=930,generated_hz=14646.8226,avg_current_mA=55.776320,avg_power_mW=257.615056,energy_mJ=16.357268,last_dac=88
FFT average sender completed
fft_average_sender,label=10k,target_hz=10000,duration_us=102400,expected_samples=1024,elapsed_us=2402,elapsed_ms=2.4020,generated_samples=320,generated_hz=133222.3147,avg_current_mA=46.689341,avg_power_mW=217.348876,energy_mJ=0.522072,last_dac=83
fft_average_sender,label=256,target_hz=256,duration_us=4000000,expected_samples=1024,elapsed_us=2486,elapsed_ms=2.4860,generated_samples=320,generated_hz=128720.8367,avg_current_mA=45.712470,avg_power_mW=215.446500,energy_mJ=0.535600,last_dac=133
FFT average sender completed
fft_average_sender,label=10k,target_hz=10000,duration_us=102400,expected_samples=1024,elapsed_us=2496,elapsed_ms=2.4960,generated_samples=320,generated_hz=128205.1282,avg_current_mA=45.403687,avg_power_mW=213.567308,energy_mJ=0.533064,last_dac=89
fft_average_sender,label=256,target_hz=256,duration_us=4000000,expected_samples=1024,elapsed_us=2495,elapsed_ms=2.4950,generated_samples=320,generated_hz=128256.5130,avg_current_mA=44.899719,avg_power_mW=210.440080,energy_mJ=0.525048,last_dac=239
FFT average sender completed
fft_average_sender,label=10k,target_hz=10000,duration_us=102400,expected_samples=1024,elapsed_us=2137,elapsed_ms=2.1370,generated_samples=310,generated_hz=145063.1727,avg_current_mA=26.732617,avg_power_mW=125.911090,energy_mJ=0.269072,last_dac=160
fft_average_sender,label=256,target_hz=256,duration_us=4000000,expected_samples=1024,elapsed_us=60497,elapsed_ms=60.4970,generated_samples=900,generated_hz=14876.7707,avg_current_mA=55.617945,avg_power_mW=259.367440,energy_mJ=15.690952,last_dac=83
FFT average sender completed
fft_average_sender,label=10k,target_hz=10000,duration_us=102400,expected_samples=1024,elapsed_us=42137,elapsed_ms=42.1370,generated_samples=710,generated_hz=16849.7995,avg_current_mA=54.258858,avg_power_mW=252.269170,energy_mJ=10.629866,last_dac=80
fft_average_sender,label=256,target_hz=256,duration_us=4000000,expected_samples=1024,elapsed_us=1691,elapsed_ms=1.6910,generated_samples=310,generated_hz=183323.4772,avg_current_mA=33.248965,avg_power_mW=155.557658,energy_mJ=0.263048,last_dac=88
FFT average sender completed
fft_average_sender,label=10k,target_hz=10000,duration_us=102400,expected_samples=1024,elapsed_us=1895,elapsed_ms=1.8950,generated_samples=310,generated_hz=163588.3905,avg_current_mA=29.563694,avg_power_mW=138.811609,energy_mJ=0.263048,last_dac=167
fft_average_sender,label=256,target_hz=256,duration_us=4000000,expected_samples=1024,elapsed_us=47617,elapsed_ms=47.6170,generated_samples=770,generated_hz=16170.6953,avg_current_mA=57.015039,avg_power_mW=260.452821,energy_mJ=12.401982,last_dac=86
FFT average sender completed
fft_average_sender,label=10k,target_hz=10000,duration_us=102400,expected_samples=1024,elapsed_us=1810,elapsed_ms=1.8100,generated_samples=290,generated_hz=160220.9945,avg_current_mA=31.273425,avg_power_mW=146.054144,energy_mJ=0.264358,last_dac=222
fft_average_sender,label=256,target_hz=256,duration_us=4000000,expected_samples=1024,elapsed_us=1817,elapsed_ms=1.8170,generated_samples=300,generated_hz=165107.3198,avg_current_mA=31.053825,avg_power_mW=142.560264,energy_mJ=0.259032,last_dac=99
FFT average sender completed
fft_average_sender,label=10k,target_hz=10000,duration_us=102400,expected_samples=1024,elapsed_us=2621,elapsed_ms=2.6210,generated_samples=320,generated_hz=122090.8050,avg_current_mA=42.861580,avg_power_mW=203.584891,energy_mJ=0.533596,last_dac=140
fft_average_sender,label=256,target_hz=256,duration_us=4000000,expected_samples=1024,elapsed_us=1905,elapsed_ms=1.9050,generated_samples=310,generated_hz=162729.6588,avg_current_mA=29.496378,avg_power_mW=136.381102,energy_mJ=0.259806,last_dac=91
FFT average sender completed
fft_average_sender,label=10k,target_hz=10000,duration_us=102400,expected_samples=1024,elapsed_us=1786,elapsed_ms=1.7860,generated_samples=310,generated_hz=173572.2284,avg_current_mA=31.680571,avg_power_mW=147.430011,energy_mJ=0.263310,last_dac=179
fft_average_sender,label=256,target_hz=256,duration_us=4000000,expected_samples=1024,elapsed_us=1490,elapsed_ms=1.4900,generated_samples=320,generated_hz=214765.1007,avg_current_mA=38.138522,avg_power_mW=176.542282,energy_mJ=0.263048,last_dac=133

## mqtt_sender
1. sampling_sender,label=10k,target_hz=10000,target_samples=50000,signal_hz=10000,period_us=100,elapsed_us=5000005,generator_hz=10403.39,avg_current_mA=53.81,avg_power_mW=252.27,energy_mJ=1261.35,power_samples=794,generator_samples=52017,last_dac=185,missed_deadlines=201
latency_probe,label=10k,request_id=1,rtt_us=0,response_received=no,response=timeout
2. sampling_sender,label=256,target_hz=256,target_samples=1280,signal_hz=10000,period_us=3906,elapsed_us=5003590,generator_hz=10401.93,avg_current_mA=45290.23,avg_power_mW=214846.25,energy_mJ=1075002.50,power_samples=723,generator_samples=52047,last_dac=220,missed_deadlines=5
latency_probe,label=256,request_id=2,rtt_us=0,response_received=no,response=timeout
sampling_sender_run,total_elapsed_us=20986000,total_elapsed_ms=20986.00

nt=50000,average=2298.9356,capture_us=5001731,payload_bytes=112,published=yes,payload=label=analog_average;case=10k;target_hz=10000;window_s=5;sample_count=50000;average=2298.9356;capture_us=5001731
mqtt_wifi_receiver_latency,response_published=yes,response=sent_us=11430185;ack_us=11485392
latency_probe,label=10k,rtt_us=63153,response_received=yes
Waiting for START pulse on GPIO5...
START pulse received
mqtt_wifi_receiver,label=256,target_hz=256,window_s=5,sample_count=1280,average=2295.2398,capture_us=4999756,payload_bytes=109,published=yes,payload=label=analog_average;case=256;target_hz=256;window_s=5;sample_count=1280;average=2295.2398;capture_us=4999756
mqtt_wifi_receiver_latency,response_published=yes,response=sent_us=16912399;ack_us=16945174
latency_probe,label=256,rtt_us=44935,response_received=yes
MQTT WiFi receiver completed
