Sample which dynamically loads libnvidia-ml.so and prints out gpu + process information.

```
$ make && _release/nvml_info
Driver Version: 418.74
NVML Version: 10.418.74
Cuda Version: 10.1

Device #0:
  Bar1: 10.38MB used of 256.00MB
  Gpu:  538.19MB used of 8116.56MB
  Gpu Processes:
      309.33MB /usr/lib/xorg/Xorg (1576)
       87.38MB cinnamon (1901)
       72.06MB /opt/vivaldi/vivaldi-bin --type=gpu-process --field-trial-handle=42842419629327 (11934)
       30.32MB ./VulkanDemos_SaschaWillems/build/bin/bloom (20077)
       33.50MB ./VulkanDemos_SaschaWillems/build/bin/sphericalenvmapping (20114)
  Compute Processes:
       30.32MB ./VulkanDemos_SaschaWillems/build/bin/bloom (20077)
       33.50MB ./VulkanDemos_SaschaWillems/build/bin/sphericalenvmapping (20114)
```
