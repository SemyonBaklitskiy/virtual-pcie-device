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
		error: 4,
		reserved : 26;
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
	union Control control_w1s; // W1S
	union Control control_w1c; // W1C
	union Status status; // RO
	uint32_t padding [48/4];
};

struct PciInferenceDevice
{
	PCIDevice pdev;
	MemoryRegion mmio_bar0; // register space
	MemoryRegion mmio_bar1; // input data
	MemoryRegion mmio_bar2; // output data
	struct RegisterSpace regspace; // 4096 / 4 = 1024
	uint8_t input_data [4096];
	uint8_t output_data [4096];
};

static uint64_t
pci_inference_device_bar0_mmio_read(void *ptr, hwaddr offset, uint32_t size)
{
	printf("pci_inference_device_bar0_mmio_read() offset = 0x%lx; size = 0x%x \n", offset, size);

	// ptr was given in memory_region_init_io() function
	struct PciInferenceDevice *device = ptr;

	uint64_t *base = (uint64_t *)(&device->regspace);

	return *(base + offset);
}

static uint64_t
pci_inference_device_bar1_mmio_read(void *ptr, hwaddr offset, uint32_t size)
{
	printf("pci_inference_device_bar1_mmio_read() offset = 0x%lx; size = 0x%x \n", offset, size);

	// ptr was given in memory_region_init_io() function
	struct PciInferenceDevice *device = ptr;

	return *((uint64_t *)(&device->input_data + offset));
}

static uint64_t
pci_inference_device_bar2_mmio_read(void *ptr, hwaddr offset, uint32_t size)
{
	printf("pci_inference_device_bar2_mmio_read() offset = 0x%lx; size = 0x%x \n", offset, size);

	// ptr was given in memory_region_init_io() function
	struct PciInferenceDevice *device = ptr;

	return *((uint64_t *)(&device->output_data + offset));
}

static void start_inference(void)
{
	// TODO: Add inference logic
	// ...
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

	// TODO: We shouldn't allow to write in RO register
	if ((offsetof(struct RegisterSpace, status) <= offset) && (offset <= offsetof(struct RegisterSpace, status)))
	{
		printf("pci_inference_device_bar0_mmio_write() couldn't write to RO register\n");
		return;
	}

	uint64_t *base = (uint64_t *)(&device->regspace);

	*(base + offset) = value;

	if (device->regspace.control.bitfields.start == 1)
	{
		device->regspace.status.bitfields.busy = 1;
		device->regspace.status.bitfields.done = 0;
		device->regspace.control.bitfields.stop = 0;
		printf("Before inference: status = 0x%x; control = 0x%x\n", device->regspace.status.value, device->regspace.control.value);

		start_inference();

		device->regspace.status.bitfields.busy = 0;
		device->regspace.status.bitfields.done = 1;
		device->regspace.control.bitfields.start = 0;
		printf("After inference: status = 0x%x; control = 0x%x\n", device->regspace.status.value, device->regspace.control.value);
	}
	else if (device->regspace.control.bitfields.stop == 1)
	{
		device->regspace.status.bitfields.busy = 0;
		device->regspace.status.bitfields.done = 0;
		device->regspace.control.bitfields.start = 0;
		printf("Before stop: status = 0x%x; control = 0x%x\n", device->regspace.status.value, device->regspace.control.value);
		stop_inference();

		device->regspace.control.bitfields.stop = 0;
		printf("After stop: status = 0x%x; control = 0x%x\n", device->regspace.status.value, device->regspace.control.value);
	}
}

static void pci_inference_device_bar1_mmio_write(void *ptr, hwaddr offset, uint64_t value,
												 uint32_t size) 
{
	struct PciInferenceDevice *device = ptr;
	*((uint64_t*)(&device->input_data + offset)) = value;
}

// Operations for the Memory Region.
static const MemoryRegionOps bar0_mmio_ops = {
	.read = pci_inference_device_bar0_mmio_read,
	.write = pci_inference_device_bar0_mmio_write,
	.endianness = DEVICE_LITTLE_ENDIAN,
	.valid = {
		.min_access_size = 1,
		.max_access_size = 4,
	},
	.impl = {
		.min_access_size = 1,
		.max_access_size = 4,
	},

};

static const MemoryRegionOps bar1_mmio_ops = {
	.read = pci_inference_device_bar1_mmio_read,
	.write = pci_inference_device_bar1_mmio_write,
	.endianness = DEVICE_LITTLE_ENDIAN,
	.valid = {
		.min_access_size = 1,
		.max_access_size = 8,
	},
	.impl = {
		.min_access_size = 1,
		.max_access_size = 8,
	},

};

static const MemoryRegionOps bar2_mmio_ops = {
	.read = pci_inference_device_bar2_mmio_read,
	.endianness = DEVICE_LITTLE_ENDIAN,
	.valid = {
		.min_access_size = 1,
		.max_access_size = 8,
	},
	.impl = {
		.min_access_size = 1,
		.max_access_size = 8,
	},
};

// implementation of the realize function.
static void pci_inference_device_realize(PCIDevice *pdev, Error **errp)
{
	struct PciInferenceDevice *device = INFERENCEDEV(pdev);
	uint8_t *pci_conf = pdev->config;

	pci_config_set_interrupt_pin(pci_conf, 1);

	// initial configuration of devices registers.
	memset((uint8_t *)(&device->regspace), 0, sizeof(device->regspace));
	memset(&device->input_data, 0, sizeof(device->input_data));
	memset(&device->output_data, 0, sizeof(device->output_data));


	// Initialize an I/O memory region(pciechodev->mmio).
	// Accesses to this region will cause the callbacks
	// of the bar0_mmio_ops to be called.
	memory_region_init_io(&device->mmio_bar0, OBJECT(device), &bar0_mmio_ops, device, "pci-inference-device-mmio", sizeof(device->regspace));
	memory_region_init_io(&device->mmio_bar1, OBJECT(device), &bar1_mmio_ops, device, "pci-inference-device-mmio", sizeof(device->input_data));
	memory_region_init_io(&device->mmio_bar2, OBJECT(device), &bar2_mmio_ops, device, "pci-inference-device-mmio", sizeof(device->output_data));

	// registering the pdev and all of the above configuration
	// (actually filling a PCI-IO region with our configuration.
	pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &device->mmio_bar0);
	pci_register_bar(pdev, 1, PCI_BASE_ADDRESS_SPACE_MEMORY, &device->mmio_bar1);
	pci_register_bar(pdev, 2, PCI_BASE_ADDRESS_SPACE_MEMORY, &device->mmio_bar2);

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
	k->revision = 0x0;							   // TODO: ?
	k->class_id = PCI_BASE_CLASS_PROCESSOR;		   // for example

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
