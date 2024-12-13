#!/bin/bash

./qemu-system-x86_64 -hda ubuntu_24.04.qcow2 -enable-kvm -smp 12 -m 16384 -device pci-inference-device -netdev bridge,id=hostnet0,br=virbr0,helper=/usr/lib/qemu/qemu-bridge-helper -device virtio-net-pci,netdev=hostnet0,id=net0 -machine q35,accel=kvm,kernel_irqchip=split -bios bios-256k.bin -device intel-iommu,intremap=on,caching-mode=on
