/*******************************************************************************
 * Copyright (C) 2009 Nathaniel McCallum <nathaniel@natemccallum.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 ******************************************************************************/

#include <windows.h>

#include "../module_types.hpp"
using namespace com::googlecode::libproxy;

#define W32REG_OFFSET_PAC  (1 << 2)
#define W32REG_OFFSET_WPAD (1 << 3)
#define W32REG_BASEKEY "Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings"

static bool _get_registry(const char *key, const char *name, uchar **sval, uint32_t *slen, uint32_t *ival) {
	HKEY  hkey;
	LONG  result;
	DWORD type;
	DWORD buflen = 1024;
	BYTE  buffer[buflen];

	// Don't allow the caller to specify both sval and ival
	if (sval && ival)
		return false;

	// Open the key
	if (RegOpenKeyExA(HKEY_CURRENT_USER, key, 0, KEY_READ, &hkey) != ERROR_SUCCESS)
		return false;

	// Read the value
	result = RegQueryValueExA(hkey, name, NULL, &type, buffer, &buflen);

	// Close the key
	RegCloseKey(hkey);

	// Evaluate
	if (result != ERROR_SUCCESS)
		return false;
	switch (type)
	{
		case REG_BINARY:
		case REG_EXPAND_SZ:
		case REG_SZ:
			if (!sval) return false;
			if (slen) *slen = buflen;
			*sval = new char[buflen];
			return !memcpy_s(*sval, buflen, buffer, buflen);
		case REG_DWORD:
			if (ival) return !memcpy_s(ival, sizeof(uint32_t), buffer, buflen);
	}
	return false;
}

static bool _is_enabled(uint8_t type) {
	uchar    *data   = NULL;
	uint32_t  dlen   = 0;
	bool      result = false;

	// Get the binary value DefaultConnectionSettings
	if (!_get_registry(W32REG_BASEKEY "\\Connections", "DefaultConnectionSettings", &data, &dlen, NULL))
		return false;

	// WPAD and PAC are contained in the 9th value
	if (dlen >= 9)
		result = data[8] & type == type; // Check to see if the bit is set

	delete data;
	return result;
}

static map<string, string> _parse_manual(string data) {
	// ProxyServer comes in two formats:
	//   1.2.3.4:8080 or ftp=1.2.3.4:8080;https=1.2.3.4:8080...
	map<string, url> rval;

	// If we have the second format, do recursive parsing,
	// then handle just the first entry
	if (data.find(";") != string::npos) {
		rval = _parse_manual(data.substr(data.find(";")+1));
		data = data.substr(0, data.find(";"));
	}

	// If we have the first format, just assign HTTP and we're done
	if (data.find("=") == string::npos) {
		rval["http"] = data;
		return rval;
	}

	// Otherwise set the value for this single entry and return
	string protocol = data.substr(0, data.find("="));
	try { rval[protocol] = url(protocol + "://" + data.substr(data.find("=")+1)); }
	catch (parse_error&) {}

	return rval;
}

class w32reg_config_module : public config_module {
public:
	PX_MODULE_ID(NULL);
	PX_MODULE_CONFIG_CATEGORY(config_module::CATEGORY_SYSTEM);

	url get_config(url dst) throw (runtime_error) {
		char        *tmp = NULL;
		uint32_t enabled = 0;

		// WPAD
		if (_is_enabled(W32REG_OFFSET_WPAD))
			return url("wpad://");

		// PAC
		if (_is_enabled(W32REG_OFFSET_PAC) &&
			_get_registry(W32REG_BASEKEY, "AutoConfigURL", &tmp, NULL, NULL) &&
			url::is_valid(string("pac+") + tmp)) {
			url cfg(string("pac+") + tmp);
			delete tmp;
			return cfg;
		}

		// Manual proxy
		// Check to see if we are enabled and get the value of ProxyServer
		if (_get_registry(W32REG_BASEKEY, "ProxyEnable", NULL, NULL, &enabled) && enabled &&
			_get_registry(W32REG_BASEKEY, "ProxyServer", &tmp, NULL, NULL)) {
			map<string, string> manual = _parse_manual(tmp);
			delete tmp;

			// First we look for an exact match
			if (manual.find(dst.get_scheme()) != map<string, url>::end)
				return manual[dst.get_scheme()];

			// Next we look for http
			else if (manual.find("http") != map<string, url>::end)
				return manual["http"];

			// Last we look for socks
			else if (manual.find("socks") != map<string, url>::end)
				return manual["socks"];
		}

		// Direct
		return url("direct://");
	}
}

PX_MODULE_LOAD(config, w32reg, true);
