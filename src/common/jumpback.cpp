/*****************************************************************************
* Copyright 2015-2018 Alexander Barthel albar965@mailbox.org
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*****************************************************************************/

#include "common/jumpback.h"

#include <navapp.h>
#include "options/optiondata.h"

JumpBack::JumpBack(QObject *parent) : QObject(parent)
{
  timer.setSingleShot(true);
  connect(&timer, &QTimer::timeout, this, &JumpBack::timeout);

}

JumpBack::~JumpBack()
{
  timer.stop();

}

bool JumpBack::isActive() const
{
  return active;
}

void JumpBack::start()
{
  start(QVariantList());
}

void JumpBack::restart()
{
  start(values);
}

void JumpBack::start(const QVariantList& jumpBackValues)
{
  if(NavApp::isConnectedAndAircraft() &&
     OptionData::instance().getFlags2() & opts::ROUTE_NO_FOLLOW_ON_MOVE)
  {
#ifdef DEBUG_INFORMATION
    qDebug() << Q_FUNC_INFO << parent()->metaObject()->className();
#endif

    if(!active)
    {
      // Do not update position if already active
      values = jumpBackValues;
      active = true;
    }

    // Restart timer
    timer.setInterval(OptionData::instance().getSimNoFollowAircraftOnScrollSeconds() * 1000);
    timer.start();
  }
}

void JumpBack::cancel()
{
#ifdef DEBUG_INFORMATION
  qDebug() << Q_FUNC_INFO << parent()->metaObject()->className();
#endif

  timer.stop();
  active = false;
}

void JumpBack::timeout()
{
#ifdef DEBUG_INFORMATION
  qDebug() << Q_FUNC_INFO << parent()->metaObject()->className();
#endif

  emit jumpBack(values);
}
