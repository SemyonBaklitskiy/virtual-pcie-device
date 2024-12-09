#include "qemu/osdep.h"
#include "qemu/units.h"
#include "hw/pci/pci.h"
#include "hw/hw.h"
#include "hw/pci/msi.h"
#include "qemu/timer.h"
#include "qom/object.h"
#include "qemu/main-loop.h" /* iothread mutex */
#include "qemu/module.h"
#include "qapi/visitor.h"

#define TYPE_PCI_CUSTOM_DEVICE "pci-inference-device"
#define PCI_INFERENCE_DEVICE_VENDOR_ID 0xCAFE

// typedef struct PciechodevState PciechodevState;

// This macro provides the instance type cast functions for a QOM type.
DECLARE_INSTANCE_CHECKER(struct PciInferenceDevice, INFERENCEDEV, TYPE_PCI_CUSTOM_DEVICE);

// TODO: Other BARs
struct PciInferenceDevice
{
	PCIDevice pdev;
	MemoryRegion mmio_bar0; // register space
	// MemoryRegion mmio_bar1; // input data
	// MemoryRegion mmio_bar2; // output data
	uint32_t bar0[1024]; // 4096 / 4 = 1024
						 // uint8_t bar1[4096];
						 // uint8_t bar2[4096];
};

// TODO: use uint_32t
struct ControlBitfields
{
	uint32_t start : 1,
		stop : 1,
		reset : 1,
		reserved : 29;
};

struct StatusBitfields
{
	uint32_t busy : 1,
		done : 1,
		// error: ...
		reserved : 30;
};

union Control
{
	uint32_t value;
	struct ControlBitfields bitfields;
};

union Status
{
	uint32_t value;
	struct StatusBitfields bitfields;
};

struct RegisterSpace
{
	union Control control; // RW
	// TODO: union Control control_w1s; // W1S
	// TODO: union Control control_w1c; // W1C
	union Status status; // RO
};

static uint64_t
pci_inference_device_bar0_mmio_read(void *ptr, hwaddr offset, uint32_t size)
{
	printf("pci_inference_device_bar0_mmio_read() offset = 0x%lx; size = 0x%x \n", offset, size);

	// ptr was given in memory_region_init_io() function
	struct PciInferenceDevice *device = ptr;

	if (offset + size > sizeof(device->bar0))
	{
		// Out of bounds
		// TODO: Maybe useless check
		printf("pci_inference_device_bar0_mmio_read() out of bar\n");
		return 0;
	}

	return device->bar0[offset];
}

static void start_inference(void)
{
	// TODO: Add inference logic
	// ...
	// reg_space->status.bitfields.busy &= 0;
	printf("start_inference() starting inference\n");
	return;
}

static void stop_inference(void)
{
	// TODO: The same
	printf("stop_inference()\n");
	return;
}

static void pci_inference_device_bar0_mmio_write(void *ptr, hwaddr offset, uint64_t value,
												 uint32_t size)
{
	printf("pci_inference_device_bar0_mmio_write() addr = 0x%lx; size = 0x%x; value = 0x%lx\n", offset, size, value);
	struct PciInferenceDevice *device = ptr;

	if (offset + size > sizeof(device->bar0))
	{
		// Out of bounds
		// TODO: Maybe useless check
		printf("pci_inference_device_bar0_mmio_write() out of bar\n");
		return;
	}

	struct RegisterSpace *reg_space = (struct RegisterSpace *)device->bar0;

	// TODO: We shouldn't allow to write in RO register
	// if ((offsetof(struct RegisterSpace, status) <= offset) && (offset <= offsetof(struct RegisterSpace, status)))
	// {
	// 	printf("pci_inference_device_bar0_mmio_write() couldn't write to RO register\n");
	// 	return;
	// }

	device->bar0[offset] = value;

	if ((reg_space->control.bitfields.start & 1) == 1)
	{
		reg_space->status.bitfields.busy |= 1;
		reg_space->status.bitfields.done &= 0;
		reg_space->control.bitfields.stop &= 0;
		printf("Before inference: status = 0x%x; control = 0x%x\n", reg_space->status.value, reg_space->control.value);

		start_inference();

		reg_space->status.bitfields.busy &= 0;
		reg_space->status.bitfields.done |= 1;
		reg_space->control.bitfields.start &= 0;
		printf("After inference: status = 0x%x; control = 0x%x\n", reg_space->status.value, reg_space->control.value);
	}
	else if ((reg_space->control.bitfields.stop & 1) == 1)
	{
		reg_space->status.bitfields.busy &= 0;
		reg_space->status.bitfields.done &= 0;
		reg_space->control.bitfields.start &= 0;
		printf("Before stop: status = 0x%x; control = 0x%x\n", reg_space->status.value, reg_space->control.value);
		stop_inference();

		reg_space->control.bitfields.stop &= 0;
		printf("After stop: status = 0x%x; control = 0x%x\n", reg_space->status.value, reg_space->control.value);
	}
}

// Operations for the Memory Region.
static const MemoryRegionOps bar0_mmio_ops = {
	.read = pci_inference_device_bar0_mmio_read,
	.write = pci_inference_device_bar0_mmio_write,
	.endianness = DEVICE_LITTLE_ENDIAN,
	.valid = {
		.min_access_size = 4,
		.max_access_size = 4,
	},
	.impl = {
		.min_access_size = 4,
		.max_access_size = 4,
	},

};

// implementation of the realize function.
static void pci_inference_device_realize(PCIDevice *pdev, Error **errp)
{
	struct PciInferenceDevice *device = INFERENCEDEV(pdev);
	uint8_t *pci_conf = pdev->config;

	pci_config_set_interrupt_pin(pci_conf, 1);

	// initial configuration of devices registers.
	memset(device->bar0, 0, sizeof(device->bar0));

	// Initialize an I/O memory region(pciechodev->mmio).
	// Accesses to this region will cause the callbacks
	// of the bar0_mmio_ops to be called.
	memory_region_init_io(&device->mmio_bar0, OBJECT(device), &bar0_mmio_ops, device, "pci-inference-device-mmio", sizeof(device->bar0));
	// registering the pdev and all of the above configuration
	// (actually filling a PCI-IO region with our configuration.
	pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &device->mmio_bar0);
}

// uninitializing functions performed.
static void pci_inference_device_instance_uninit(PCIDevice *pdev)
{
	return;
}

// initialization of the device
static void pci_inference_device_instance_init(Object *obj)
{
	printf("pci_inference_device_instance_init\n");
	return;
}

static void pci_inference_device_class_init(ObjectClass *class, void *data)
{
	printf("pci_inference_device_class_init\n");
	DeviceClass *dc = DEVICE_CLASS(class);
	PCIDeviceClass *k = PCI_DEVICE_CLASS(class);

	// definition of realize func().
	k->realize = pci_inference_device_realize;
	// definition of uninit func().
	k->exit = pci_inference_device_instance_uninit;
	k->vendor_id = PCI_VENDOR_ID_QEMU;
	k->device_id = PCI_INFERENCE_DEVICE_VENDOR_ID; // our device id, 'cafe' hexadecimal
	k->revision = 0x0;
	k->class_id = PCI_BASE_CLASS_PROCESSOR; // for example

	/**
	 * set_bit - Set a bit in memory
	 * @nr: the bit to set
	 * @addr: the address to start counting from
	 */
	set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static InterfaceInfo interfaces[] = {
	{INTERFACE_CONVENTIONAL_PCI_DEVICE},
	{},
};

static const TypeInfo pci_inference_device_info = {
	.name = TYPE_PCI_CUSTOM_DEVICE,
	.parent = TYPE_PCI_DEVICE,
	.instance_size = sizeof(struct PciInferenceDevice),
	.instance_init = pci_inference_device_instance_init,
	.class_init = pci_inference_device_class_init,
	.interfaces = interfaces,
};

static void pci_inference_device_register_types(void)
{
	printf("pci_custom_device_register_types\n");
	// registers the new type.
	type_register_static(&pci_inference_device_info);
}

type_init(pci_inference_device_register_types)
