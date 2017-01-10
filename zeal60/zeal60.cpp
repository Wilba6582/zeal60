
#include <stdint.h>
#include <stdio.h>

#include <cstring>
#include <string>
#include <map>
#include <iostream>

// Headers needed for sleeping.
#ifdef _WIN32
#include <windows.h>
#include "hidapi.h"
#else
#include <unistd.h>
#include "hidapi/hidapi.h"
#endif

#ifdef _MSC_VER
// Stop compiler bitching about sscanf() being unsafe.
// Prefer the portable sscanf() to the suggested, MSVC-only sscanf_c()
#pragma warning(disable:4996)
#endif

#include "config.h"
#include "keycode.h"

#include "../../qmk_firmware/keyboards/zeal60/zeal_rpc.h"
#include "../../qmk_firmware/keyboards/zeal60/zeal_color.h"

hid_device *
hid_open( unsigned short vendor_id, unsigned short product_id, unsigned short usage_page, unsigned short usage )
{
	hid_device *device = NULL;
	struct hid_device_info *deviceInfos;
	struct hid_device_info *currentDeviceInfo;
	struct hid_device_info *foundDeviceInfo = NULL;
	deviceInfos = hid_enumerate( vendor_id, product_id );
	currentDeviceInfo = deviceInfos;
	while ( currentDeviceInfo )
	{
		//usage_page and usage are windows/mac only
		#ifndef __linux__
		if ( currentDeviceInfo->usage_page == usage_page &&
			 currentDeviceInfo->usage == usage )
		{
		#else
		if ( currentDeviceInfo->interface_number == 2 )
		{
		#endif
			if ( foundDeviceInfo )
			{
				// More than one matching device.
				// TODO: return error?
			}
			else
			{
				foundDeviceInfo = currentDeviceInfo;
			}
		}
		currentDeviceInfo = currentDeviceInfo->next;
	}

	if ( foundDeviceInfo )
	{
		device = hid_open_path( foundDeviceInfo->path );
	}

	hid_free_enumeration(deviceInfos);

	return device;
}

bool send_message( hid_device *device, uint8_t id, void *outMsg = NULL, uint8_t outMsgLength = 0, void *retMsg = NULL, uint8_t retMsgLength = 0 )
{
	int res;
	uint8_t data[RAW_HID_BUFFER_SIZE + 1];
	memset( data, 0xFE, sizeof( data ) );
	data[0] = 0x00; // NULL report ID. IMPORTANT!
	data[1] = id;

	if ( outMsg && outMsgLength > 0 )
	{
		memcpy( &data[2], outMsg, outMsgLength );
	}

	res = 0;
	res = hid_write( device, data, RAW_HID_BUFFER_SIZE + 1 );
	if ( res < 0 )
	{
		printf( "Unable to write()\n" );
		printf( "Error: %ls\n", hid_error( device ) );
		return false;
	}

	hid_set_nonblocking( device, 1 );

	res = 0;
	// Timeout after 500ms
	for ( int i=0; i<500; i++ )
	{
		res = hid_read( device, data, RAW_HID_BUFFER_SIZE );
		if ( res != 0 )
		{
			break;
		}
		// waiting
#ifdef WIN32
		Sleep( 1 );
#else
		usleep( 1 * 1000 );
#endif
	}

	if ( res < 0 )
	{
		printf( "Unable to read()\n" );
		printf( "Error: %ls\n", hid_error( device ) );
		return false;
	}

	if ( res > 0 )
	{
		if ( data[1] == id_unhandled )
		{
			return false;
		}

		if ( retMsg && retMsgLength > 0 )
		{
			memcpy( retMsg, &data[1], retMsgLength );
		}
	}

	return true;
}

bool protocol_version( hid_device *device, msg_protocol_version *msg )
{
	return send_message( device, id_protocol_version, msg, sizeof(msg_protocol_version), msg, sizeof(msg_protocol_version) );
}

bool keymap_keycode_load( hid_device *device, msg_keymap_keycode_load *msg )
{
	return send_message( device, id_keymap_keycode_load, msg, sizeof(msg_keymap_keycode_load), msg, sizeof(msg_keymap_keycode_load) );
}

bool keymap_keycode_save( hid_device *device, msg_keymap_keycode_save *msg )
{
	return send_message( device, id_keymap_keycode_save, msg, sizeof(msg_keymap_keycode_save) );
}

bool keymap_default_save( hid_device *device )
{
	return send_message( device, id_keymap_default_save );
}

bool backlight_config_set_values( hid_device *device, msg_backlight_config_set_values *msg )
{
	return send_message( device, id_backlight_config_set_values, msg, sizeof(msg_backlight_config_set_values) );
}

bool backlight_config_set_alphas_mods( hid_device *device, msg_backlight_config_set_alphas_mods *msg )
{
	return send_message( device, id_backlight_config_set_alphas_mods, msg, sizeof(msg_backlight_config_set_alphas_mods) );
}

bool backlight_set_key_color( hid_device *device, msg_backlight_set_key_color *msg )
{
	return send_message( device, id_backlight_set_key_color, msg, sizeof(msg_backlight_set_key_color) );
}

int main(int argc, char **argv)
{
	if (hid_init())
	{
		std::cerr << "*** Error: hidapi initialization failed" << std::endl;
		return -1;
	}

	hid_device *device = hid_open( DEVICE_VID, DEVICE_PID, DEVICE_USAGE_PAGE, DEVICE_USAGE );
	if ( ! device )
	{
		std::cerr << "*** Error: Device not found" << std::endl;
		return -1;
	}

	bool res = false;
	msg_protocol_version msg;
	if ( res == protocol_version( device, &msg ) )
	{
		std::cerr << "*** Error: Error getting protocol version" << std::endl;
		hid_close( device );
		return -1;
	}

	if ( msg.version != PROTOCOL_VERSION )
	{
		std::cerr << "*** Error: Device uses protocol version " << msg.version << std::endl;
		std::cerr << "This program uses protocol version " << PROTOCOL_VERSION << std::endl;
		hid_close( device );
		return -1;
	}

	if (argc <= 1)
	{
		// No args, do nothing
		return 0;
	}

	// First arg is the command
	std::string command = argv[1];

	if ( command == "backlight_config_set_values" )
	{
		msg_backlight_config_set_values msg;
		memset( (void*)&msg, 0x00, sizeof(msg_backlight_config_set_values) );

		msg.use_split_backspace = 0;
		msg.use_split_left_shift = 0;
		msg.use_split_right_shift = 0;
		msg.use_7u_spacebar = 0;
		msg.use_iso_enter = 0;
		msg.disable_when_usb_suspended = 0;
		msg.disable_after_timeout = 0;

		for ( int i = 2; i < argc; i++ )
		{
			std::string s = argv[i];

			// Handle "name=value" cases first
			size_t equalPos = s.find( "=" );
			if ( equalPos == std::string::npos )
			{
				std::cerr << "*** Error: Invalid value '" << s << "', not in form 'name=value'" << std::endl;
				return -1;
			}

			std::string name = s.substr( 0, equalPos );
			std::string value = s.substr( equalPos + 1 );
			int intValue = 0;

			if ( name == "use_split_backspace" &&
				sscanf( value.c_str(), "%d", &intValue ) == 1 )
			{
				msg.use_split_backspace = intValue != 0;
			}
			else if ( name == "use_split_left_shift" &&
				sscanf( value.c_str(), "%d", &intValue ) == 1 )
			{
				msg.use_split_left_shift = intValue != 0;
			}
			else if ( name == "use_split_right_shift" &&
				sscanf( value.c_str(), "%d", &intValue ) == 1 )
			{
				msg.use_split_right_shift = intValue != 0;
			}
			else if ( name == "use_7u_spacebar" &&
				sscanf( value.c_str(), "%d", &intValue ) == 1 )
			{
				msg.use_7u_spacebar = intValue != 0;
			}
			else if ( name == "use_iso_enter" &&
				sscanf( value.c_str(), "%d", &intValue ) == 1 )
			{
				msg.use_iso_enter = intValue != 0;
			}
			else if ( name == "disable_when_usb_suspended" &&
				sscanf( value.c_str(), "%d", &intValue ) == 1 )
			{
				msg.disable_when_usb_suspended = intValue != 0;
			}
			else if ( name == "disable_after_timeout" &&
				sscanf( value.c_str(), "%d", &intValue ) == 1 )
			{
				msg.disable_after_timeout = intValue;
			}
			else
			{
				std::cerr << "*** Error: Invalid name/value '" << s << "'" << std::endl;
				return -1;
			}
		}

		if ( !backlight_config_set_values( device, &msg ) )
		{
			std::cerr << "*** Error: Error setting backlight config values" << std::endl;
			hid_close( device );
			return -1;
		}

		hid_close( device );
		std::cout << "Backlight config values set" << std::endl;
		return 0;
	}
	else if ( command == "backlight_config_set_alphas_mods" )
	{
		if ( argc < 2 + MATRIX_ROWS * MATRIX_COLS )
		{
			std::cerr << "*** Error: Invalid number of arguments for '" << command << "'" << std::endl;
			return -1;
		}
		msg_backlight_config_set_alphas_mods msg;
		int arg = 2;
		for ( int row = 0; row < MATRIX_ROWS; row++ )
		{
			msg.alphas_mods[row] = 0;
			for ( int col = 0; col < MATRIX_COLS; col++ )
			{
				std::string s = argv[arg];
				if ( s == "ALPHA" )
				{
					// leave it 0
				}
				else if ( s == "MOD" )
				{
					msg.alphas_mods[row] |= (0b0010000000000000 >> col);
				}
				else
				{
					std::cerr << "Invalid alphas/mods string '" << argv[arg] << "'" << std::endl;
					return -1;
				}

				arg++;
			}
		}

		if (!backlight_config_set_alphas_mods( device, &msg ) )
		{
			std::cerr << "*** Error: Error setting backlight config alpha/mods" << std::endl;
			hid_close( device );
			return -1;
		}

		hid_close( device );
		std::cout << "Backlight config alphas/mods set" << std::endl;
		return 0;



	}
	else if (command == "keymap")
	{
		if (argc < 2 + 1 + MATRIX_ROWS * MATRIX_COLS)
		{
			std::cerr << "*** Error: Invalid number of arguments for '" << command << "'" << std::endl;
			return -1;
		}
		int layer = atoi(argv[2]);
		if ( layer < 0 || layer > 3)
		{
			std::cerr << "*** Error: Invalid layer '" << argv[2] << "'" << std::endl;
			return -1;
		}

		int keymap[MATRIX_ROWS][MATRIX_COLS];
		int arg = 3;
		for ( int row = 0; row < MATRIX_ROWS; row++ )
		{
			for (int col = 0; col < MATRIX_COLS; col++)
			{
				uint16_t value;
				if ( stringToValue(argv[arg], &value ) )
				{
					keymap[row][col] = value;
				}
				else
				{
					std::cerr << "*** Error: Invalid keycode '" << argv[arg] << "'" << std::endl;
					return -1;
				}

				arg++;
			}

		}

		for (int row = 0; row < MATRIX_ROWS; row++)
		{
			for (int column = 0; column < MATRIX_COLS; column++)
			{
				msg_keymap_keycode_save msg;
				msg.layer = layer;
				msg.row = row;
				msg.column = column;
				msg.keycode = keymap[row][column];
				if ( !keymap_keycode_save( device, &msg ) )
				{
					std::cerr << "*** Error: Error saving keymap layer " << layer << " row " << row << " column " << column << std::endl;
					hid_close( device );
					return -1;
				}
			}
		}

		hid_close( device );
		std::cout << "Keymap layer " << layer << " saved" << std::endl;
		return 0;
	}
	else if ( command == "colors" )
	{
		HSV colors[MATRIX_ROWS][MATRIX_COLS];
		std::map<std::string, HSV > mapNameToColor;

		int colorsIndex = 0;
		int arg = 2;
		for ( int arg = 2; arg < argc; arg++ )
		{
			std::string argString = argv[arg];

			// Find "NAME=hsv(120,100,100)"
			size_t equalPos = argString.find( "=" );
			if ( equalPos != std::string::npos )
			{
				std::string name = argString.substr( 0, equalPos );
				std::string colorString = argString.substr( equalPos + 1 );
				if ( colorString.find( "hsv(" ) == 0 &&
					colorString.find( ")" ) == colorString.length() - 1 )
				{
					std::string values = colorString.substr( 4, colorString.length() - 1 );
					int h, s, v;
					if ( sscanf( values.c_str(), "%d,%d,%d", &h, &s, &v ) == 3 )
					{
						// we're good.
						HSV color;
						color.h = uint8_t( double( h ) / 360.0 * 255.0 );
						color.s = uint8_t( double( s ) / 100.0 * 255.0 );
						color.v = uint8_t( double( v ) / 100.0 * 255.0 );
						mapNameToColor.insert( std::pair<std::string, HSV>( name, color ) );
						continue;
					}
				}
				else
				{
					std::cerr << "*** Error: Invalid color assignment '" << argString << "'" << std::endl;
					return -1;
				}
			}
			else
			{
				std::map<std::string,HSV>::iterator it = mapNameToColor.find( argString );
				if ( it != mapNameToColor.end() )
				{
					int row = colorsIndex / MATRIX_COLS;
					int col = colorsIndex % MATRIX_COLS;
					if ( row >= MATRIX_ROWS )
					{
						// Too many entries!
						std::cerr << "*** Error: Invalid number of color entries" << std::endl;
						return -1;
					}
					colors[row][col] = it->second;
					colorsIndex++;
				}
				else
				{
					std::cerr << "*** Error: Invalid color entry '" << argString << "'" << std::endl;
					return -1;
				}
			}
		}

		if ( colorsIndex != MATRIX_ROWS * MATRIX_COLS )
		{
			std::cerr << "*** Error: Invalid number of color entries" << std::endl;
			return -1;
		}

		for ( int row = 0; row < MATRIX_ROWS; row++ )
		{
			for ( int column = 0; column < MATRIX_COLS; column++ )
			{
				msg_backlight_set_key_color msg;
				msg.row = row;
				msg.column = column;
				msg.hsv = colors[row][column];
				if ( !backlight_set_key_color( device, &msg ) )
				{
					std::cerr << "*** Error: Error saving color row " << row << " column " << column << std::endl;
					hid_close( device );
					return -1;
				}
			}
		}

		hid_close( device );
		std::cout << "Keymap colors saved" << std::endl;
		return 0;
	}

	std::cerr << "*** Error: Invalid command '" << command << "'" << std::endl;
	return -1;
}

