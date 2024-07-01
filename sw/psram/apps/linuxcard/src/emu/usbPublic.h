/*
	(c) 2021 Dmitry Grinberg   https://dmitry.gr
	Non-commercial use only OR licensing@dmitry.gr
*/

#ifndef _USB_PUBLIC_H_
#define _USB_PUBLIC_H_

//assumes an LE machine

#include <stdint.h>


#define DESCR_TYP_DEVICE			1
#define DESCR_TYP_CONFIG			2
#define DESCR_TYP_STRING			3
#define DESCR_TYP_IFACE				4
#define DESCR_TYP_ENDPT				5
#define DESCR_TYP_DEV_QUAL			6
#define DESCR_TYP_OTH_SPD_CFG		7
#define DESCR_TYP_INT_PWR			8
#define DESCR_TYP_ASSOC				11

//HID
#define DESCR_TYP_HID				0x21
#define DESCR_TYP_HID_REPORT		0x22
#define DESCR_TYP_HID_PHYS			0x23

//CDC
#define DESCR_TYP_CDC_CS_IFACE		0x24
#define DESCR_TYP_CDC_CS_ENDPT		0x25

#define DESCR_SUBTYP_CDC_HDR		0x00
#define DESCR_SUBTYP_CDC_CALL_MGMNT	0x01
#define DESCR_SUBTYP_CDC_ACM		0x02
#define DESCR_SUBTYP_CDC_UNION		0x06

struct UsbDeviceDescriptor {
	uint8_t bLength;				//should be sizeof(struct UsbDeviceDescriptor)
	uint8_t bDescriptorType;		//should be DESCR_TYP_DEVICE
	uint16_t bcdUSB;				//probaly should be 0x0200
	uint8_t bDeviceClass;
	uint8_t bDeviceSubClass;
	uint8_t bDeviceProtocol;
	uint8_t bMaxPacketSize0;
	uint16_t idVendor;
	uint16_t idProduct;
	uint16_t bcdDevice;
	uint8_t iManufacturer;			//string index
	uint8_t iProduct;				//string index
	uint8_t iSerialNumber;			//string index
	uint8_t bNumConfigurations;
} __attribute__((packed));

struct UsbConfigDescriptor {
	uint8_t bLength;				//should be sizeof(struct UsbDeviceDescriptor)
	uint8_t bDescriptorType;		//should be DESCR_TYP_CONFIG
	uint16_t numBytesReturned;		//total length of data returned
	uint8_t numIfaces;
	uint8_t thisCfgIdx;
	uint8_t iCfgNameIdx;
	uint8_t bAttributes;
	uint8_t bCurrent;				//in units of 2MA
} __attribute__((packed));

struct UsbStringDescriptor {
	uint8_t bLength;				//should be sizeof(struct UsbStringDescriptor) + sizeof(uint16_t) * num_chars
	uint8_t bDescriptorType;		//should be DESCR_TYP_CONFIG
	uint16_t chars[];
} __attribute__((packed));

#define USB_DESCR_EP_NO_MASK_IN		0x80
#define USB_EP_DESCR_EP_TYP_CTL		0x00
#define USB_EP_DESCR_EP_TYP_ISO		0x01
#define USB_EP_DESCR_EP_TYP_BULK	0x02
#define USB_EP_DESCR_EP_TYP_INTR	0x03


struct UsbEndpointDescriptor {
	uint8_t bLength;				//should be sizeof(struct UsbEndpointDescriptor)
	uint8_t bDescriptorType;		//should be DESCR_TYP_ENDPT
	uint8_t bEndpointAddress;		//USB_DESCR_EP_NO_MASK_IN orr-ed in for IN EPs
	uint8_t bmAttributes;			//USB_EP_DESCR_EP_TYP_*, other flags for iso EPs
	uint16_t wMaxPacketSize;
	uint8_t bInterval;
} __attribute__((packed));

struct UsbInterfaceDescriptor {
	uint8_t bLength;				//should be sizeof(struct UsbEndpointDescriptor)
	uint8_t bDescriptorType;		//should be DESCR_TYP_ENDPT
	uint8_t bInterfaceNumber;		//iface ID
	uint8_t bAlternateSetting;		//alternate setting inside the iface
	uint8_t bNumEndpoints;
	uint8_t bInterfaceClass;
	uint8_t bInterfaceSubClass;
	uint8_t bInterfaceProtocol;
	uint8_t iInterface;				//string index
} __attribute__((packed));

struct UsbCdcHeaderFunctionalDescriptor {
	uint8_t bFunctionLength;		//should be sizeof(struct UsbCdcHeaderFunctionalDescriptor)
	uint8_t bDescriptorType;		//should be DESCR_TYP_CDC_CS_IFACE
	uint8_t bDescriptorSubtype;		//should be DESCR_SUBTYP_CDC_HDR
	uint16_t bcdCDC;				//probably 0x0110
} __attribute__((packed));

struct UsbCdcAcmFunctionalDescriptor {
	uint8_t bFunctionLength;		//should be sizeof(struct UsbCdcAcmFunctionalDescriptor)
	uint8_t bDescriptorType;		//should be DESCR_TYP_CDC_CS_IFACE
	uint8_t bDescriptorSubtype;		//should be DESCR_SUBTYP_CDC_ACM
	uint8_t bmCapabilities;			
} __attribute__((packed));

struct UsbCdcUnionFunctionalDescriptor {
	uint8_t bFunctionLength;		//should be sizeof(struct UsbCdcUnionFunctionalDescriptor)
	uint8_t bDescriptorType;		//should be DESCR_TYP_CDC_CS_IFACE
	uint8_t bDescriptorSubtype;		//should be DESCR_SUBTYP_CDC_UNION
	uint8_t bMasterInterface;
	uint8_t bSlaveInterface0;		//in theory more are allowed, in practice nobody supports that
} __attribute__((packed));

struct UsbCdcCallManagementFunctionalDescriptor {
	uint8_t bFunctionLength;		//should be sizeof(struct UsbCdcCallManagementFunctionalDescriptor)
	uint8_t bDescriptorType;		//should be DESCR_TYP_CDC_CS_IFACE
	uint8_t bDescriptorSubtype;		//should be DESCR_SUBTYP_CDC_CALL_MGMNT
	uint8_t bmCapabilities;
	uint8_t bDataInterface;			//iface index
} __attribute__((packed));

struct IfaceAssocDescriptor {
	uint8_t bLength;
	uint8_t bDescriptorType;		//should be DESCR_TYP_ASSOC
 	uint8_t bFirstInterface;
 	uint8_t bInterfaceCount;
 	uint8_t bFunctionClass;
 	uint8_t bFunctionSubClass;
 	uint8_t bFunctionProtocol;
 	uint8_t iFunction;
} __attribute__((packed));



#endif
