/* src/prism2/driver/prism2mib.c
*
* Management request for mibset/mibget
*
* Copyright (C) 1999 AbsoluteValue Systems, Inc.  All Rights Reserved.
* --------------------------------------------------------------------
*
* linux-wlan
*
*   The contents of this file are subject to the Mozilla Public
*   License Version 1.1 (the "License"); you may not use this file
*   except in compliance with the License. You may obtain a copy of
*   the License at http://www.mozilla.org/MPL/
*
*   Software distributed under the License is distributed on an "AS
*   IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or
*   implied. See the License for the specific language governing
*   rights and limitations under the License.
*
*   Alternatively, the contents of this file may be used under the
*   terms of the GNU Public License version 2 (the "GPL"), in which
*   case the provisions of the GPL are applicable instead of the
*   above.  If you wish to allow the use of your version of this file
*   only under the terms of the GPL and not to allow others to use
*   your version of this file under the MPL, indicate your decision
*   by deleting the provisions above and replace them with the notice
*   and other provisions required by the GPL.  If you do not delete
*   the provisions above, a recipient may use your version of this
*   file under either the MPL or the GPL.
*
* --------------------------------------------------------------------
*
* Inquiries regarding the linux-wlan Open Source project can be
* made directly to:
*
* AbsoluteValue Systems Inc.
* info@linux-wlan.com
* http://www.linux-wlan.com
*
* --------------------------------------------------------------------
*
* Portions of the development of this software were funded by
* Intersil Corporation as part of PRISM(R) chipset product development.
*
* --------------------------------------------------------------------
*
* The functions in this file handle the mibset/mibget management
* functions.
*
* --------------------------------------------------------------------
*/

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/wireless.h>
#include <linux/netdevice.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <asm/byteorder.h>
#include <linux/usb.h>
#include <linux/bitops.h>

#include "p80211types.h"
#include "p80211hdr.h"
#include "p80211mgmt.h"
#include "p80211conv.h"
#include "p80211msg.h"
#include "p80211netdev.h"
#include "p80211metadef.h"
#include "p80211metastruct.h"
#include "hfa384x.h"
#include "prism2mgmt.h"

#define MIB_TMP_MAXLEN    200	

#define  F_STA        0x1	
#define  F_READ       0x2	
#define  F_WRITE      0x4	/* MIB may be written. */

struct mibrec {
	u32 did;
	u16 flag;
	u16 parm1;
	u16 parm2;
	u16 parm3;
	int (*func) (struct mibrec *mib,
		     int isget,
		     wlandevice_t *wlandev,
		     hfa384x_t *hw,
		     struct p80211msg_dot11req_mibset *msg, void *data);
};

static int prism2mib_bytearea2pstr(struct mibrec *mib,
				   int isget,
				   wlandevice_t *wlandev,
				   hfa384x_t *hw,
				   struct p80211msg_dot11req_mibset *msg,
				   void *data);

static int prism2mib_uint32(struct mibrec *mib,
			    int isget,
			    wlandevice_t *wlandev,
			    hfa384x_t *hw,
			    struct p80211msg_dot11req_mibset *msg, void *data);

static int prism2mib_flag(struct mibrec *mib,
			  int isget,
			  wlandevice_t *wlandev,
			  hfa384x_t *hw,
			  struct p80211msg_dot11req_mibset *msg, void *data);

static int prism2mib_wepdefaultkey(struct mibrec *mib,
				   int isget,
				   wlandevice_t *wlandev,
				   hfa384x_t *hw,
				   struct p80211msg_dot11req_mibset *msg,
				   void *data);

static int prism2mib_privacyinvoked(struct mibrec *mib,
				    int isget,
				    wlandevice_t *wlandev,
				    hfa384x_t *hw,
				    struct p80211msg_dot11req_mibset *msg,
				    void *data);

static int prism2mib_excludeunencrypted(struct mibrec *mib,
					int isget,
					wlandevice_t *wlandev,
					hfa384x_t *hw,
					struct p80211msg_dot11req_mibset *msg,
					void *data);

static int prism2mib_fragmentationthreshold(struct mibrec *mib,
					    int isget,
					    wlandevice_t *wlandev,
					    hfa384x_t *hw,
					    struct p80211msg_dot11req_mibset *msg,
					    void *data);

static int prism2mib_priv(struct mibrec *mib,
			  int isget,
			  wlandevice_t *wlandev,
			  hfa384x_t *hw,
			  struct p80211msg_dot11req_mibset *msg, void *data);

static struct mibrec mibtab[] = {

	
	{DIDmib_dot11smt_dot11WEPDefaultKeysTable_dot11WEPDefaultKey0,
	 F_STA | F_WRITE,
	 HFA384x_RID_CNFWEPDEFAULTKEY0, 0, 0,
	 prism2mib_wepdefaultkey},
	{DIDmib_dot11smt_dot11WEPDefaultKeysTable_dot11WEPDefaultKey1,
	 F_STA | F_WRITE,
	 HFA384x_RID_CNFWEPDEFAULTKEY1, 0, 0,
	 prism2mib_wepdefaultkey},
	{DIDmib_dot11smt_dot11WEPDefaultKeysTable_dot11WEPDefaultKey2,
	 F_STA | F_WRITE,
	 HFA384x_RID_CNFWEPDEFAULTKEY2, 0, 0,
	 prism2mib_wepdefaultkey},
	{DIDmib_dot11smt_dot11WEPDefaultKeysTable_dot11WEPDefaultKey3,
	 F_STA | F_WRITE,
	 HFA384x_RID_CNFWEPDEFAULTKEY3, 0, 0,
	 prism2mib_wepdefaultkey},
	{DIDmib_dot11smt_dot11PrivacyTable_dot11PrivacyInvoked,
	 F_STA | F_READ | F_WRITE,
	 HFA384x_RID_CNFWEPFLAGS, HFA384x_WEPFLAGS_PRIVINVOKED, 0,
	 prism2mib_privacyinvoked},
	{DIDmib_dot11smt_dot11PrivacyTable_dot11WEPDefaultKeyID,
	 F_STA | F_READ | F_WRITE,
	 HFA384x_RID_CNFWEPDEFAULTKEYID, 0, 0,
	 prism2mib_uint32},
	{DIDmib_dot11smt_dot11PrivacyTable_dot11ExcludeUnencrypted,
	 F_STA | F_READ | F_WRITE,
	 HFA384x_RID_CNFWEPFLAGS, HFA384x_WEPFLAGS_EXCLUDE, 0,
	 prism2mib_excludeunencrypted},

	

	{DIDmib_dot11mac_dot11OperationTable_dot11MACAddress,
	 F_STA | F_READ | F_WRITE,
	 HFA384x_RID_CNFOWNMACADDR, HFA384x_RID_CNFOWNMACADDR_LEN, 0,
	 prism2mib_bytearea2pstr},
	{DIDmib_dot11mac_dot11OperationTable_dot11RTSThreshold,
	 F_STA | F_READ | F_WRITE,
	 HFA384x_RID_RTSTHRESH, 0, 0,
	 prism2mib_uint32},
	{DIDmib_dot11mac_dot11OperationTable_dot11ShortRetryLimit,
	 F_STA | F_READ,
	 HFA384x_RID_SHORTRETRYLIMIT, 0, 0,
	 prism2mib_uint32},
	{DIDmib_dot11mac_dot11OperationTable_dot11LongRetryLimit,
	 F_STA | F_READ,
	 HFA384x_RID_LONGRETRYLIMIT, 0, 0,
	 prism2mib_uint32},
	{DIDmib_dot11mac_dot11OperationTable_dot11FragmentationThreshold,
	 F_STA | F_READ | F_WRITE,
	 HFA384x_RID_FRAGTHRESH, 0, 0,
	 prism2mib_fragmentationthreshold},
	{DIDmib_dot11mac_dot11OperationTable_dot11MaxTransmitMSDULifetime,
	 F_STA | F_READ,
	 HFA384x_RID_MAXTXLIFETIME, 0, 0,
	 prism2mib_uint32},

	

	{DIDmib_dot11phy_dot11PhyDSSSTable_dot11CurrentChannel,
	 F_STA | F_READ,
	 HFA384x_RID_CURRENTCHANNEL, 0, 0,
	 prism2mib_uint32},
	{DIDmib_dot11phy_dot11PhyTxPowerTable_dot11CurrentTxPowerLevel,
	 F_STA | F_READ | F_WRITE,
	 HFA384x_RID_TXPOWERMAX, 0, 0,
	 prism2mib_uint32},

	

	{DIDmib_p2_p2Static_p2CnfPortType,
	 F_STA | F_READ | F_WRITE,
	 HFA384x_RID_CNFPORTTYPE, 0, 0,
	 prism2mib_uint32},

	

	{DIDmib_p2_p2MAC_p2CurrentTxRate,
	 F_STA | F_READ,
	 HFA384x_RID_CURRENTTXRATE, 0, 0,
	 prism2mib_uint32},

	
	{DIDmib_lnx_lnxConfigTable_lnxRSNAIE,
	 F_STA | F_READ | F_WRITE,
	 HFA384x_RID_CNFWPADATA, 0, 0,
	 prism2mib_priv},
	{0, 0, 0, 0, 0, NULL}
};


int prism2mgmt_mibset_mibget(wlandevice_t *wlandev, void *msgp)
{
	hfa384x_t *hw = wlandev->priv;
	int result, isget;
	struct mibrec *mib;

	u16 which;

	struct p80211msg_dot11req_mibset *msg = msgp;
	p80211itemd_t *mibitem;

	msg->resultcode.status = P80211ENUM_msgitem_status_data_ok;
	msg->resultcode.data = P80211ENUM_resultcode_success;


	which = F_STA;


	mibitem = (p80211itemd_t *) msg->mibattribute.data;

	for (mib = mibtab; mib->did != 0; mib++)
		if (mib->did == mibitem->did && (mib->flag & which))
			break;

	if (mib->did == 0) {
		msg->resultcode.data = P80211ENUM_resultcode_not_supported;
		goto done;
	}

	/*
	 ** Determine if this is a "mibget" or a "mibset".  If this is a
	 ** "mibget", then make sure that the MIB may be read.  Otherwise,
	 ** this is a "mibset" so make make sure that the MIB may be written.
	 */

	isget = (msg->msgcode == DIDmsg_dot11req_mibget);

	if (isget) {
		if (!(mib->flag & F_READ)) {
			msg->resultcode.data =
			    P80211ENUM_resultcode_cant_get_writeonly_mib;
			goto done;
		}
	} else {
		if (!(mib->flag & F_WRITE)) {
			msg->resultcode.data =
			    P80211ENUM_resultcode_cant_set_readonly_mib;
			goto done;
		}
	}


	result = mib->func(mib, isget, wlandev, hw, msg, (void *)mibitem->data);

	if (msg->resultcode.data == P80211ENUM_resultcode_success) {
		if (result != 0) {
			pr_debug("get/set failure, result=%d\n", result);
			msg->resultcode.data =
			    P80211ENUM_resultcode_implementation_failure;
		} else {
			if (isget) {
				msg->mibattribute.status =
				    P80211ENUM_msgitem_status_data_ok;
				mibitem->status =
				    P80211ENUM_msgitem_status_data_ok;
			}
		}
	}

done:
	return 0;
}


static int prism2mib_bytearea2pstr(struct mibrec *mib,
				   int isget,
				   wlandevice_t *wlandev,
				   hfa384x_t *hw,
				   struct p80211msg_dot11req_mibset *msg,
				   void *data)
{
	int result;
	p80211pstrd_t *pstr = (p80211pstrd_t *) data;
	u8 bytebuf[MIB_TMP_MAXLEN];

	if (isget) {
		result =
		    hfa384x_drvr_getconfig(hw, mib->parm1, bytebuf, mib->parm2);
		prism2mgmt_bytearea2pstr(bytebuf, pstr, mib->parm2);
	} else {
		memset(bytebuf, 0, mib->parm2);
		prism2mgmt_pstr2bytearea(bytebuf, pstr);
		result =
		    hfa384x_drvr_setconfig(hw, mib->parm1, bytebuf, mib->parm2);
	}

	return result;
}


static int prism2mib_uint32(struct mibrec *mib,
			    int isget,
			    wlandevice_t *wlandev,
			    hfa384x_t *hw,
			    struct p80211msg_dot11req_mibset *msg, void *data)
{
	int result;
	u32 *uint32 = (u32 *) data;
	u8 bytebuf[MIB_TMP_MAXLEN];
	u16 *wordbuf = (u16 *) bytebuf;

	if (isget) {
		result = hfa384x_drvr_getconfig16(hw, mib->parm1, wordbuf);
		*uint32 = *wordbuf;
	} else {
		*wordbuf = *uint32;
		result = hfa384x_drvr_setconfig16(hw, mib->parm1, *wordbuf);
	}

	return result;
}


static int prism2mib_flag(struct mibrec *mib,
			  int isget,
			  wlandevice_t *wlandev,
			  hfa384x_t *hw,
			  struct p80211msg_dot11req_mibset *msg, void *data)
{
	int result;
	u32 *uint32 = (u32 *) data;
	u8 bytebuf[MIB_TMP_MAXLEN];
	u16 *wordbuf = (u16 *) bytebuf;
	u32 flags;

	result = hfa384x_drvr_getconfig16(hw, mib->parm1, wordbuf);
	if (result == 0) {
		flags = *wordbuf;
		if (isget) {
			*uint32 = (flags & mib->parm2) ?
			    P80211ENUM_truth_true : P80211ENUM_truth_false;
		} else {
			if ((*uint32) == P80211ENUM_truth_true)
				flags |= mib->parm2;
			else
				flags &= ~mib->parm2;
			*wordbuf = flags;
			result =
			    hfa384x_drvr_setconfig16(hw, mib->parm1, *wordbuf);
		}
	}

	return result;
}


static int prism2mib_wepdefaultkey(struct mibrec *mib,
				   int isget,
				   wlandevice_t *wlandev,
				   hfa384x_t *hw,
				   struct p80211msg_dot11req_mibset *msg,
				   void *data)
{
	int result;
	p80211pstrd_t *pstr = (p80211pstrd_t *) data;
	u8 bytebuf[MIB_TMP_MAXLEN];
	u16 len;

	if (isget) {
		result = 0;	
	} else {
		len = (pstr->len > 5) ? HFA384x_RID_CNFWEP128DEFAULTKEY_LEN :
		    HFA384x_RID_CNFWEPDEFAULTKEY_LEN;
		memset(bytebuf, 0, len);
		prism2mgmt_pstr2bytearea(bytebuf, pstr);
		result = hfa384x_drvr_setconfig(hw, mib->parm1, bytebuf, len);
	}

	return result;
}


static int prism2mib_privacyinvoked(struct mibrec *mib,
				    int isget,
				    wlandevice_t *wlandev,
				    hfa384x_t *hw,
				    struct p80211msg_dot11req_mibset *msg,
				    void *data)
{
	int result;

	if (wlandev->hostwep & HOSTWEP_DECRYPT) {
		if (wlandev->hostwep & HOSTWEP_DECRYPT)
			mib->parm2 |= HFA384x_WEPFLAGS_DISABLE_RXCRYPT;
		if (wlandev->hostwep & HOSTWEP_ENCRYPT)
			mib->parm2 |= HFA384x_WEPFLAGS_DISABLE_TXCRYPT;
	}

	result = prism2mib_flag(mib, isget, wlandev, hw, msg, data);

	return result;
}


static int prism2mib_excludeunencrypted(struct mibrec *mib,
					int isget,
					wlandevice_t *wlandev,
					hfa384x_t *hw,
					struct p80211msg_dot11req_mibset *msg,
					void *data)
{
	int result;

	result = prism2mib_flag(mib, isget, wlandev, hw, msg, data);

	return result;
}


static int prism2mib_fragmentationthreshold(struct mibrec *mib,
					    int isget,
					    wlandevice_t *wlandev,
					    hfa384x_t *hw,
					    struct p80211msg_dot11req_mibset *msg,
					    void *data)
{
	int result;
	u32 *uint32 = (u32 *) data;

	if (!isget)
		if ((*uint32) % 2) {
			printk(KERN_WARNING "Attempt to set odd number "
			       "FragmentationThreshold\n");
			msg->resultcode.data =
			    P80211ENUM_resultcode_not_supported;
			return 0;
		}

	result = prism2mib_uint32(mib, isget, wlandev, hw, msg, data);

	return result;
}


static int prism2mib_priv(struct mibrec *mib,
			  int isget,
			  wlandevice_t *wlandev,
			  hfa384x_t *hw,
			  struct p80211msg_dot11req_mibset *msg, void *data)
{
	p80211pstrd_t *pstr = (p80211pstrd_t *) data;

	int result;

	switch (mib->did) {
	case DIDmib_lnx_lnxConfigTable_lnxRSNAIE:{
			hfa384x_WPAData_t wpa;
			if (isget) {
				hfa384x_drvr_getconfig(hw,
						       HFA384x_RID_CNFWPADATA,
						       (u8 *) &wpa,
						       sizeof(wpa));
				pstr->len = le16_to_cpu(wpa.datalen);
				memcpy(pstr->data, wpa.data, pstr->len);
			} else {
				wpa.datalen = cpu_to_le16(pstr->len);
				memcpy(wpa.data, pstr->data, pstr->len);

				result =
				    hfa384x_drvr_setconfig(hw,
						   HFA384x_RID_CNFWPADATA,
						   (u8 *) &wpa,
						   sizeof(wpa));
			}
			break;
		}
	default:
		printk(KERN_ERR "Unhandled DID 0x%08x\n", mib->did);
	}

	return 0;
}


void prism2mgmt_pstr2bytestr(hfa384x_bytestr_t *bytestr, p80211pstrd_t *pstr)
{
	bytestr->len = cpu_to_le16((u16) (pstr->len));
	memcpy(bytestr->data, pstr->data, pstr->len);
}


void prism2mgmt_pstr2bytearea(u8 *bytearea, p80211pstrd_t *pstr)
{
	memcpy(bytearea, pstr->data, pstr->len);
}


void prism2mgmt_bytestr2pstr(hfa384x_bytestr_t *bytestr, p80211pstrd_t *pstr)
{
	pstr->len = (u8) (le16_to_cpu((u16) (bytestr->len)));
	memcpy(pstr->data, bytestr->data, pstr->len);
}


void prism2mgmt_bytearea2pstr(u8 *bytearea, p80211pstrd_t *pstr, int len)
{
	pstr->len = (u8) len;
	memcpy(pstr->data, bytearea, len);
}