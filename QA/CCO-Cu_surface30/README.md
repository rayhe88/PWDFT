
# Timings 
## laptop timings
| machine     | ncpus  |   cputime | non-local |       ffm |       fmf |       fft | diagonalize |  
| :----:      | :----: |       ---:|        --:|        --:|        --:|      ---: |          --:| 
| **QA/CCO-Cu_surface30**
| mac-m1      | 1      |  |  |  |  |  |  |  |
| mac-m1      | 2      |  |  |  |  |  |  |  |
| mac-m1      | 4      |  |  |  |  |  |  |  |
| mac-m1      | 6      |  |  |  |  |  |  |  | 
| mac-m1      | 8      |  |  |  |  |  |  |  |
|      |      |  |  |  |  |  |  | |
| WE45090     | 1      |  |  |  |  |  |  |  |
| WE45090     | 2      |  |  |  |  |  |  |  |
| WE45090     | 4      |  |  |  |  |  |  |  |
| WE45090     | 6      |  |  |  |  |  |  |  |
| WE45090     | 8      |  |  |  |  |  |  |  |
|      |      |  |  |  |  |  |  | |
| **WE45090-GPU** | **1**  |  |  |  |  |  |  |  |
| WE45090-GPU | 2      |  |  |  |  |  |  |  |
| WE45090-GPU | 4      |  |  |  |  |  |  |  |
| WE45090-GPU | 6      |  |  |  |  |  |  |  |
| WE45090-GPU | 8      |  |  |  |  |  |  |  |
|      |      |  |  |  |  |  |  | |
| perlmutter-GPU | 1   | 1.144e+01 | 1.449e+00 | 1.075e-01 | 1.357e-01 | 8.811e+00 | 5.817e-03 |
| perlmutter-GPU | 2   | 6.682e+00 | 9.810e-01 | 7.965e-02 | 6.936e-02 | 4.887e+00 | 5.874e-03 |
| perlmutter-GPU | 3   | 6.033e+00 | 9.408e-01 | 6.408e-02 | 8.597e-02 | 4.281e+00 | 4.273e-03 |
| perlmutter-GPU | 4   | 5.216e+00 | 8.570e-01 | 5.502e-02 | 4.573e-02 | 3.549e+00 | 4.368e-03 |
|      |      |  |  |  |  |  |  | |
| perlmutter-CPU | 1   |  |  |  |  |  |  |  |
| perlmutter-CPU | 2   |  |  |  |  |  |  |  |
| perlmutter-CPU | 3   |  |  |  |  |  |  |  |
| perlmutter-CPU | 4   |  |  |  |  |  |  |  |
| perlmutter-CPU | 6   |  |  |  |  |  |  |  |
| perlmutter-CPU | 8   | 2.023e+01 | 8.142e+00 | 5.626e+00 | 3.662e+00 | 1.952e+00 | 6.117e-03 |
| perlmutter-CPU | 16  | 1.886e+01 | 7.164e+00 | 5.660e+00 | 3.875e+00 | 1.153e+00 | 7.070e-03 |
| perlmutter-CPU | 32  | 8.414e+00 | 2.960e+00 | 2.524e+00 | 1.041e+00 | 7.433e-01 | 7.290e-03 | 
| perlmutter-CPU | 64  | 5.349e+00 | 1.519e+00 | 1.562e+00 | 6.833e-01 | 5.196e-01 | 7.450e-03 | 
| perlmutter-CPU | 128 | 4.099e+00 | 1.227e+00 | 9.893e-01 | 4.387e-01 | 3.770e-01 | 9.901e-03 | 