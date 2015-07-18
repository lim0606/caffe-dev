#!/usr/bin/env sh

./build/tools/caffe train --solver=examples/mnist/lenet_bn_solver.prototxt --gpu=0
