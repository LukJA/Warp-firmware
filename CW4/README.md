# CW4 - Activity Classifier Source

| Name: | Luke Andrews |
| ----- | ----- |
| College: | Gonville & Caius |
| CrsID: | LJA45 |

## Contents and Use with Warp Firmware

The core activity classifier code is found in the packaged `recog.c` and `recog.h`, and accesses the accelerometer data by wrapping routines in the warp `devMMA8451Q.*` device driver. In order to run the classifier, copy the `recog` files to the source directory, and apply the included diff patches to `boot.c` and `config.h` using `patch` to enable the necessary build flags. Additionally, apply the supplied patches to the top-level MakeFile and to `CMakeLists-FRDMKL03.txt` to include the files in the build system.

Alternatively, the entire repository can be cloned from https://github.com/LukJA/Warp-firmware.git, and built and uploaded with `make clean frdmkl03 load-warp`. Compiled binaries in `hex` and `srec` are also included for direct upload.

## Project Overview

This project implements an activity classifier designed to identify and distinguish 'normal' walking against being stationary, sitting, standing, or any other activity, and provide an probability estimate for this classification. This is designed exclusively to run on the FRDM-KL03 development board from kinetis, under the warp firmware.

Inspiration for the implementation of feature creation was taken from *bayat et al* - "A Study on Human Activity Recognition Using Accelerometer Data from Smartphones" - 
<a href="https://doi.org/10.1016/j.procs.2014.07.009">10.1016/j.procs.2014.07.009</a> 

The classification algorithm is based on a simplified `Naive Bayes Classifier`, that has been adapted and approximated to run quickly without floating point and without logarithm or exponential functions, and using power-of-2 divide. The classifier used the following features measured over a window of 64 samples at 50Hz with a dynamic range of $\pm$ 4g, for a window period of 1.28s:

- Mean Low-Pass filtered values of X, Y and Z
- Mean High-Pass filtered values of X, Y and Z	
- Variance of the High-Pass filtered values of X, Y and Z
- Min-Max variation of High-Pass filtered values of X, Y and Z

A 0.5 Hz low-pass filter derived from *bayat et al* generates the LP and HP version of each signal. The FRD

Training was completed using the python `sklearn` implementation of a Gaussian Naive Bayes Classifier, `Gaussian NB`, and ported to c for execution on the development board. The algorithm is executed using integer fixed point arithmetic to give 2 decimal places of precision, and the final exponential function is implemented using a look-up table to give final 2 decimal place probabilities. 