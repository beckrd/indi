/*
    Losmandy Gemini INDI driver

    Copyright (C) 2017 Jasem Mutlaq

    Difference from LX200 Generic:

    1. Added Side of Pier
    2. Reimplemented isSlewComplete to use :Gv# since it is more reliable

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
*/

#include <math.h>
#include <termios.h>
#include "lx200gemini.h"

LX200Gemini::LX200Gemini()
{
    setVersion(1, 2);

    SetTelescopeCapability(TELESCOPE_CAN_PARK | TELESCOPE_CAN_SYNC | TELESCOPE_CAN_GOTO | TELESCOPE_CAN_ABORT | TELESCOPE_HAS_TIME | TELESCOPE_HAS_LOCATION | TELESCOPE_HAS_PIER_SIDE,4);
}

const char * LX200Gemini::getDefaultName()
{
    return (char *)"Losmandy Gemini";
}

void LX200Gemini::ISGetProperties(const char *dev)
{
    LX200Generic::ISGetProperties(dev);

    defineSwitch(&StartupModeSP);
    loadConfig(true, StartupModeSP.name);
}

bool LX200Gemini::initProperties()
{
    LX200Generic::initProperties();

    // Park Option
    IUFillSwitch(&ParkOptionS[PARK_HOME], "HOME", "Home", ISS_ON);
    IUFillSwitch(&ParkOptionS[PARK_STARTUP], "STARTUP", "Startup", ISS_OFF);
    IUFillSwitch(&ParkOptionS[PARK_ZENITH], "ZENITH", "Zenith", ISS_OFF);
    IUFillSwitchVector(&ParkOptionsSP, ParkOptionS, 3, getDeviceName(), "PARK_POSITION", "Park Position", MAIN_CONTROL_TAB, IP_RW, ISR_1OFMANY, 60, IPS_IDLE);

    IUFillSwitch(&StartupModeS[COLD_START], "COLD_START", "Cold", ISS_ON);
    IUFillSwitch(&StartupModeS[PARK_STARTUP], "WARM_START", "Warm", ISS_OFF);
    IUFillSwitch(&StartupModeS[PARK_ZENITH], "WARM_RESTART", "Restart", ISS_OFF);
    IUFillSwitchVector(&StartupModeSP, StartupModeS, 3, getDeviceName(), "STARTUP_MODE", "Startup Mode", MAIN_CONTROL_TAB, IP_RW, ISR_1OFMANY, 60, IPS_IDLE);
}

bool LX200Gemini::updateProperties()
{
    LX200Generic::updateProperties();

    if (isConnected())
    {
        defineSwitch(&ParkOptionSP);
    }
    else
    {
        deleteProperty(ParkOptionSP.name);
    }

    return true;
}

bool LX200Gemini::ISNewSwitch (const char *dev, const char *name, ISState *states, char *names[], int n)
{
    if(!strcmp(dev,getDeviceName()))
    {
        if (!strcmp(name, StartupModeSP.name))
        {
            IUUpdateSwitch(&StartupModeSP, states, names, n);
            StartupModeSP.s = IPS_OK;

            DEBUG(INDI::Logger::DBG_SESSION, "Startup mode will take effect on future connections.");
            IDSetSwitch(&StartupModeSP, NULL);
            return true;
        }

        if (!strcmp(name, ParkOptionSP.name))
        {
            IUUpdateSwitch(&ParkOptionSP, states, names, n);
            ParkOptionSP.s = IPS_OK;
            IDSetSwitch(&ParkOptionsSP, NULL);
            return true;
        }
    }

    return LX200Generic::ISNewSwitch(dev, name, states, names, n);
}

bool LX200Gemini::checkConnection()
{
    // Response
    char response[2] = {0};
    int rc=0, nbytes_read=0, nbytes_written=0;

    DEBUGF(INDI::Logger::DBG_DEBUG, "CMD: <%#02X>", 0x06);

    tcflush(PortFD, TCIOFLUSH);

    char ack[1] = { 0x06 };

    if ( (rc = tty_write(PortFD, ack, 1, &nbytes_written)) != TTY_OK)
    {
        char errmsg[256];
        tty_error_msg(rc, errmsg, 256);
        DEBUGF(INDI::Logger::DBG_ERROR, "Error writing to device %s (%d)", errmsg, rc);
        return false;
    }

    // Read response
    if ( (rc = tty_read_section(PortFD, response, '#', GEMINI_TIMEOUT, &nbytes_read)) != TTY_OK)
    {
        char errmsg[256];
        tty_error_msg(rc, errmsg, 256);
        DEBUGF(INDI::Logger::DBG_ERROR, "Error reading from device %s (%d)", errmsg, rc);
        return false;
    }

    response[1] = '\0';

    tcflush(PortFD, TCIOFLUSH);

    DEBUGF(INDI::Logger::DBG_DEBUG, "RES: <%s>", response);

    // If waiting for selection of startup mode, let us select it
    if (response[0] == 'b')
    {
        DEBUG(INDI::Logger::DBG_DEBUG, "Mount is waiting for selection of the startup mode.");
        char cmd[4] = "bC#";
        int startupMode = IUFindOnSwitchIndex(&StartupModeSP);
        if (startupMode == WARM_START)
            strncpy(cmd, "bW#", 4);
        else if (startupMode == WARM_RESTART)
            strncpy(cmd, "bR#", 4);

        DEBUGF(INDI::Logger::DBG_DEBUG, "CMD: <%s>", cmd);

        if ( (rc = tty_write(PortFD, cmd, 4, &nbytes_written)) != TTY_OK)
        {
            char errmsg[256];
            tty_error_msg(rc, errmsg, 256);
            DEBUGF(INDI::Logger::DBG_ERROR, "Error writing to device %s (%d)", errmsg, rc);
            return false;
        }

        // Send ack again and check response
        return checkConnection();
    }
    else if (response[0] == 'B')
    {
        DEBUG(INDI::Logger::DBG_DEBUG, "Initial startup message is being displayed.");
    }
    else if (response[0] == 'S')
    {
        DEBUG(INDI::Logger::DBG_DEBUG, "Cold start in progress.");
    }
    else if (response[0] == 'G')
    {
        DEBUG(INDI::Logger::DBG_DEBUG, "Startup complete with equatorial mount selected.");
    }
    else if (response[0] == 'A')
    {
        DEBUG(INDI::Logger::DBG_DEBUG, "Startup complete with Alt-Az mount selected.");
    }

    return true;
}

bool LX200Gemini::isSlewComplete()
{
    // Send ':Gv#'
    const char * cmd = "#:Gv#";
    // Response
    char response[2] = {0};
    int rc=0, nbytes_read=0, nbytes_written=0;

    DEBUGF(INDI::Logger::DBG_DEBUG, "CMD: <%s>", cmd);

    tcflush(PortFD, TCIOFLUSH);

    if ( (rc = tty_write(PortFD, cmd, 5, &nbytes_written)) != TTY_OK)
    {
        char errmsg[256];
        tty_error_msg(rc, errmsg, 256);
        DEBUGF(INDI::Logger::DBG_ERROR, "Error writing to device %s (%d)", errmsg, rc);
        return false;
    }

    // Read 1 character
    if ( (rc = tty_read(PortFD, response, 1, GEMINI_TIMEOUT, &nbytes_read)) != TTY_OK)
    {
        char errmsg[256];
        tty_error_msg(rc, errmsg, 256);
        DEBUGF(INDI::Logger::DBG_ERROR, "Error reading from device %s (%d)", errmsg, rc);
        return false;
    }

    response[1] = '\0';

    tcflush(PortFD, TCIOFLUSH);

    DEBUGF(INDI::Logger::DBG_DEBUG, "RES: <%s>", response);

    if (response[0] == 'T' || response[0] == 'G' || response[0] == 'N')
        return true;
    else
        return false;
}

bool LX200Gemini::ReadScopeStatus()
{
   syncSideOfPier();

   return LX200Generic::ReadScopeStatus();
}

void LX200Gemini::syncSideOfPier()
{
    // Send ':Gv#'
    const char * cmd = "#:Gm#";
    // Response
    char response[2] = {0};
    int rc=0, nbytes_read=0, nbytes_written=0;

    DEBUGF(INDI::Logger::DBG_DEBUG, "CMD: <%s>", cmd);

    tcflush(PortFD, TCIOFLUSH);

    if ( (rc = tty_write(PortFD, cmd, 5, &nbytes_written)) != TTY_OK)
    {
        char errmsg[256];
        tty_error_msg(rc, errmsg, 256);
        DEBUGF(INDI::Logger::DBG_ERROR, "Error writing to device %s (%d)", errmsg, rc);
        return;
    }

    // Read 1 character
    if ( (rc = tty_read_section(PortFD, response, '#', GEMINI_TIMEOUT, &nbytes_read)) != TTY_OK)
    {
        char errmsg[256];
        tty_error_msg(rc, errmsg, 256);
        DEBUGF(INDI::Logger::DBG_ERROR, "Error reading from device %s (%d)", errmsg, rc);
        return;
    }

    response[nbytes_read-1] = '\0';

    tcflush(PortFD, TCIOFLUSH);

    DEBUGF(INDI::Logger::DBG_DEBUG, "RES: <%s>", response);

    setPierSide(response[0] == 'E' ? INDI::Telescope::PIER_EAST : INDI::Telescope::PIER_WEST);
}

bool LX200Gemini::Park()
{
    char cmd[6] = "#:hP#";

    int parkOption = IUFindOnSwitchIndex(&ParkOptionSP);

    if (parkOption == PARK_STARTUP)
        strncpy(cmd, "#:hC#", 5);
    else if (parkOption == PARK_ZENITH)
        strncpy(cmd, "#:hZ#", 5);

    // Response
    int rc=0, nbytes_written=0;

    DEBUGF(INDI::Logger::DBG_DEBUG, "CMD: <%s>", cmd);

    tcflush(PortFD, TCIOFLUSH);

    if ( (rc = tty_write(PortFD, cmd, 5, &nbytes_written)) != TTY_OK)
    {
        char errmsg[256];
        tty_error_msg(rc, errmsg, 256);
        DEBUGF(INDI::Logger::DBG_ERROR, "Error writing to device %s (%d)", errmsg, rc);
        return false;
    }

    ParkSP.s = IPS_BUSY;
    TrackState = SCOPE_PARKING;
    return true;
}

bool LX200Gemini::UnPark()
{
    const char *cmd = "#:hN#";

    // Response
    int rc=0, nbytes_written=0;

    DEBUGF(INDI::Logger::DBG_DEBUG, "CMD: <%s>", cmd);

    tcflush(PortFD, TCIOFLUSH);

    if ( (rc = tty_write(PortFD, cmd, 5, &nbytes_written)) != TTY_OK)
    {
        char errmsg[256];
        tty_error_msg(rc, errmsg, 256);
        DEBUGF(INDI::Logger::DBG_ERROR, "Error writing to device %s (%d)", errmsg, rc);
        return false;
    }

    TrackState = SCOPE_IDLE;
    return true;
}

bool LX200Gemini::saveConfigItems(FILE *fp)
{
    LX200Generic::saveConfigItems(fp);

    IUSaveConfigSwitch(fp, &StartupModeSP);
    IUSaveConfigSwitch(fp, &ParkOptionSP);

    return true;
}
