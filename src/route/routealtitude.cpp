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

#include "route/routealtitude.h"

#include "route/route.h"
#include "atools.h"
#include "geo/calculations.h"
#include "fs/perf/aircraftperf.h"

#include <QLineF>

using atools::interpolate;

RouteAltitude::RouteAltitude(const Route *routeParam)
  : route(routeParam)
{

}

RouteAltitude::~RouteAltitude()
{

}

float RouteAltitude::getAltitudeForDistance(float distanceToDest) const
{
  if(isEmpty())
    return map::INVALID_ALTITUDE_VALUE;

  float distFromStart = route->getTotalDistance() - distanceToDest;

  // Search through all legs to find one that overlaps with distanceToDest
  QVector<RouteAltitudeLeg>::const_iterator it =
    std::lower_bound(begin(), end(), distFromStart,
                     [](const RouteAltitudeLeg& l1, float dist) -> bool
  {
    // binary predicate which returns ​true if the first argument is less than (i.e. is ordered before) the second.
    return l1.getDistanceFromStart() < dist;
  });

  if(it != end())
  {
    // Now search through the geometry to find a matching line (if more than one)
    const RouteAltitudeLeg& leg = *it;

    QPolygonF::const_iterator itGeo =
      std::lower_bound(leg.geometry.begin(), leg.geometry.end(), distFromStart,
                       [](const QPointF& pt, float dist) -> bool
    {
      // binary predicate which returns ​true if the first argument is less than (i.e. is ordered before) the second.
      return pt.x() < dist;
    });

    if(itGeo != leg.geometry.end())
    {
      QPointF pt2 = *itGeo;
      QPointF pt1 = *(--itGeo);

      // Interpolate and try to find point and altitude for given distance
      QLineF line(pt1, pt2);
      return static_cast<float>(line.pointAt((distFromStart - pt1.x()) / line.dx()).y());
    }
  }
  return map::INVALID_ALTITUDE_VALUE;
}

float RouteAltitude::getTravelTimeHours() const
{
  return travelTime;
}

float RouteAltitude::getTopOfDescentFromDestination() const
{
  if(isEmpty())
    return map::INVALID_DISTANCE_VALUE;
  else
    return route->getTotalDistance() - distanceTopOfDescent;
}

atools::geo::Pos RouteAltitude::getTopOfDescentPos() const
{
  // Avoid any invalid points near destination
  if(isEmpty() || !(distanceTopOfDescent < map::INVALID_DISTANCE_VALUE) ||
     distanceTopOfDescent > route->getTotalDistance() - 0.2f)
    return atools::geo::EMPTY_POS;
  else
    return route->getPositionAtDistance(distanceTopOfDescent);
}

atools::geo::Pos RouteAltitude::getTopOfClimbPos() const
{
  // Avoid any invalid points near departure
  if(isEmpty() || !(distanceTopOfClimb < map::INVALID_DISTANCE_VALUE) || distanceTopOfClimb < 0.2f)
    return atools::geo::EMPTY_POS;
  else
    return route->getPositionAtDistance(distanceTopOfClimb);
}

void RouteAltitude::clearAll()
{
  clear();
  distanceTopOfClimb = map::INVALID_DISTANCE_VALUE;
  distanceTopOfDescent = map::INVALID_DISTANCE_VALUE;
  legIndexTopOfClimb = map::INVALID_INDEX_VALUE;
  legIndexTopOfDescent = map::INVALID_INDEX_VALUE;
  destRunwayIls.clear();
  destRunwayEnd = map::MapRunwayEnd();
  travelTime = 0.f;
  averageGroundSpeed = 0.f;
  unflyableLegs = false;
}

float RouteAltitude::getTotalDistance() const
{
  return route->getTotalDistance();
}

bool RouteAltitude::hasErrors() const
{
  return altRestrictionsViolated() || !(getTopOfDescentDistance() < map::INVALID_DISTANCE_VALUE &&
                                        getTopOfClimbDistance() < map::INVALID_DISTANCE_VALUE);
}

QStringList RouteAltitude::getErrorStrings(QString& tooltip) const
{
  QStringList messages;

  if(altRestrictionsViolated())
  {
    messages << tr("Cannot comply with altitude restrictions.");
    tooltip = tr("Invalid Flight Plan.\n"
                 "Check the flight plan cruise altitude and procedures.");
  }
  else if(!(getTopOfDescentDistance() < map::INVALID_DISTANCE_VALUE &&
            getTopOfClimbDistance() < map::INVALID_DISTANCE_VALUE))
  {
    messages << tr("Cannot calculate top of climb or top of descent.");
    tooltip = tr("Invalid Flight Plan or Aircraft Performance.\n"
                 "Check the flight plan cruise altitude and\n"
                 "climb/descent speeds in the Aircraft Performance.");
  }

  return messages;
}

void RouteAltitude::adjustAltitudeForRestriction(RouteAltitudeLeg& leg) const
{
  if(!leg.isEmpty())
    leg.setY2(adjustAltitudeForRestriction(leg.y2(), leg.restriction));
}

float RouteAltitude::adjustAltitudeForRestriction(float altitude, const proc::MapAltRestriction& restriction) const
{
  switch(restriction.descriptor)
  {
    case proc::MapAltRestriction::NONE:
      break;

    case proc::MapAltRestriction::AT:
      altitude = restriction.alt1;
      break;

    case proc::MapAltRestriction::AT_OR_ABOVE:
      if(altitude < restriction.alt1)
        altitude = restriction.alt1;
      break;

    case proc::MapAltRestriction::AT_OR_BELOW:
      if(altitude > restriction.alt1)
        altitude = restriction.alt1;
      break;

    case proc::MapAltRestriction::BETWEEN:
      if(altitude > restriction.alt1)
        altitude = restriction.alt1;

      if(altitude < restriction.alt2)
        altitude = restriction.alt2;
      break;
  }
  return altitude;
}

bool RouteAltitude::violatesAltitudeRestriction(const RouteAltitudeLeg& leg) const
{
  if(!leg.isEmpty())
  {
    switch(leg.restriction.descriptor)
    {
      case proc::MapAltRestriction::NONE:
        return false;

      case proc::MapAltRestriction::AT:
        return atools::almostNotEqual(leg.y2(), leg.restriction.alt1, 10.f);

      case proc::MapAltRestriction::AT_OR_ABOVE:
        return leg.y2() < leg.restriction.alt1;

      case proc::MapAltRestriction::AT_OR_BELOW:
        return leg.y2() > leg.restriction.alt1;

      case proc::MapAltRestriction::BETWEEN:
        return leg.y2() > leg.restriction.alt1 || leg.y2() < leg.restriction.alt2;
    }
  }
  return false;
}

float RouteAltitude::findApproachMaxAltitude(int index) const
{
  if(index > 1)
  {
    // Avoid crashes
    index = fixRange(index);

    if(index < map::INVALID_INDEX_VALUE)
    {
      // Check backwards from index for a arrival/STAR leg that limits the maximum altitude
      for(int i = index - 1; i >= 0; i--)
      {
        const RouteLeg& leg = route->at(i);

        if(leg.isAnyProcedure() && leg.getProcedureLeg().isAnyArrival() && leg.getProcedureLegAltRestr().isValid())
        {
          const proc::MapAltRestriction& r = leg.getProcedureLegAltRestr();
          if(r.descriptor == proc::MapAltRestriction::AT || r.descriptor == proc::MapAltRestriction::AT_OR_BELOW ||
             r.descriptor == proc::MapAltRestriction::BETWEEN)
            return r.alt1;
        }
      }
    }
    else
      qWarning() << Q_FUNC_INFO;
  }
  return map::INVALID_ALTITUDE_VALUE;
}

float RouteAltitude::findDepartureMaxAltitude(int index) const
{
  if(index > 1)
  {
    // Avoid crashes
    index = fixRange(index);

    // Check forward from index to end of SID for leg that limits the maximum altitude
    int end = fixRange(route->getDepartureLegsOffset() + route->getDepartureLegs().size());
    if(end == map::INVALID_INDEX_VALUE)
      // Search through the whole route
      end = route->size() - 1;

    if(index < map::INVALID_INDEX_VALUE && end < map::INVALID_INDEX_VALUE)
    {
      for(int i = index; i < end; i++)
      {
        const RouteLeg& leg = route->at(i);

        if(leg.isAnyProcedure() && leg.getProcedureLeg().isAnyDeparture() && leg.getProcedureLegAltRestr().isValid())
        {
          const proc::MapAltRestriction& r = leg.getProcedureLegAltRestr();
          if(r.descriptor == proc::MapAltRestriction::AT || r.descriptor == proc::MapAltRestriction::AT_OR_BELOW ||
             r.descriptor == proc::MapAltRestriction::BETWEEN)
            return r.alt1;
        }
      }
    }
    else
      qWarning() << Q_FUNC_INFO;
  }
  return map::INVALID_ALTITUDE_VALUE;
}

int RouteAltitude::findApproachFirstRestricion() const
{
  if(route->hasAnyArrivalProcedure() || route->hasAnyStarProcedure())
  {
    // Search for first restriction in an arrival or STAR
    int start = route->getStarLegsOffset();
    if(!(start < map::INVALID_INDEX_VALUE))
      // No STAR - use arrival
      start = route->getArrivalLegsOffset();

    if(start < map::INVALID_INDEX_VALUE)
    {
      for(int i = start; i < route->size(); i++)
      {
        const RouteLeg& leg = route->at(i);
        if(leg.isAnyProcedure() && leg.getProcedureLeg().isAnyArrival() && leg.getProcedureLegAltRestr().isValid())
          return i;
      }
    }
    else
      qWarning() << Q_FUNC_INFO;
  }
  return map::INVALID_INDEX_VALUE;
}

int RouteAltitude::findDepartureLastRestricion() const
{
  if(route->hasAnyDepartureProcedure())
  {
    // Search for first restriction in a SID
    int start = fixRange(route->getDepartureLegsOffset() + route->getDepartureLegs().size());

    if(start < map::INVALID_INDEX_VALUE)
    {
      for(int i = start; i > 0; i--)
      {
        const RouteLeg& leg = route->at(i);

        if(leg.isAnyProcedure() && leg.getProcedureLeg().isAnyDeparture() && leg.getProcedureLegAltRestr().isValid())
          return i;
      }
    }
    else
      qWarning() << Q_FUNC_INFO;
  }
  return map::INVALID_INDEX_VALUE;
}

int RouteAltitude::fixRange(int index) const
{
  if(index < map::INVALID_INDEX_VALUE)
    return std::min(std::max(index, 0), size() - 1);

  return index;
}

void RouteAltitude::simplyfyRouteAltitudes()
{
  // Flatten descent legs starting from TOD to destination
  if(legIndexTopOfDescent < map::INVALID_INDEX_VALUE && legIndexTopOfDescent >= 0)
  {
    // Use eight iterations
    for(int i = 0; i < 8; i++)
    {
#ifdef DEBUG_INFORMATION
      qDebug() << Q_FUNC_INFO << "=================================================";
#endif
      for(int j = legIndexTopOfDescent; j < size() - 1; j++)
        simplifyRouteAltitude(j, false /* departure */);
    }
  }
  else
    qWarning() << Q_FUNC_INFO;

  // Flatten departute legs starting from departure to TOC
  if(legIndexTopOfClimb < map::INVALID_INDEX_VALUE && legIndexTopOfClimb >= 0)
  {
    // Use eight iterations
    for(int i = 0; i < 8; i++)
    {
   #ifdef DEBUG_INFORMATION
      qDebug() << Q_FUNC_INFO << "=================================================";
   #endif
      for(int j = 1; j < legIndexTopOfClimb; j++)
        simplifyRouteAltitude(j, true /* departure */);
    }
  }
  else
    qWarning() << Q_FUNC_INFO;
}

void RouteAltitude::simplifyRouteAltitude(int index, bool departure)
{
#ifdef DEBUG_INFORMATION
  qDebug() << Q_FUNC_INFO;
#endif

  if(index <= 0 || index >= size() - 1)
  {
    qWarning() << Q_FUNC_INFO << "index <= 0 || index >= size() - 1";
    return;
  }

  RouteAltitudeLeg *midAlt = &(*this)[index];

  RouteAltitudeLeg *leftAlt = &(*this)[index - 1];
  RouteAltitudeLeg *rightAlt = &(*this)[index + 1];
  RouteAltitudeLeg *rightSkippedAlt = nullptr, *leftSkippedAlt = nullptr;
  proc::MapAltRestriction restriction = midAlt->restriction;

  if(atools::almostEqual(midAlt->getDistanceFromStart(), leftAlt->getDistanceFromStart()) && index >= 2)
  {
    if(leftAlt->geometry.size() <= 2)
    {
      if(!restriction.isValid() && leftAlt->restriction.isValid())
        // Take the altitude restriction from one of the duplicate points
        restriction = leftAlt->restriction;

      // This duplicate leg with the same postion (IAF, etc.) will be skipped
      leftSkippedAlt = &(*this)[index - 1];

      // Adjust leg if two equal legs (IAF) are after each other - otherwise no change will be done
      leftAlt = &(*this)[index - 2];
    }
  }

  if(atools::almostEqual(midAlt->getDistanceFromStart(), rightAlt->getDistanceFromStart()) && index < size() - 2)
  {
    if(rightAlt->geometry.size() <= 2)
    {
      if(!restriction.isValid() && rightAlt->restriction.isValid())
        // Take the altitude restriction from one of the duplicate points
        restriction = rightAlt->restriction;

      // This duplicate leg with the same postion (IAF, etc.) will be skipped
      rightSkippedAlt = &(*this)[index + 1];

      // Adjust leg if two equal legs (IAF) are after each other - otherwise no change will be done
      rightAlt = &(*this)[index + 2];
    }
  }

#ifdef DEBUG_INFORMATION
  qDebug() << Q_FUNC_INFO << leftAlt->ident << midAlt->ident << rightAlt->ident << "departure" << departure;
#endif

  // Avoid dummy legs (e.g. missed approach)
  if(!leftAlt->isEmpty() && !rightAlt->isEmpty() && !midAlt->isEmpty())
  {
    QPointF leftPt = leftAlt->asPoint();
    QPointF rightPt = rightAlt->asPoint();

    if(midAlt->geometry.size() >= 3)
    {
      // This leg contains a TOD or/and TOC
      if(departure)
        // Use TOD/TOC position from this instead of the one from the right leg
        rightPt = midAlt->geometry.at(1);
      else
        // Arrival
        // Use TOD/TOC position from this instead of the one from the left leg
        leftPt = midAlt->geometry.at(midAlt->geometry.size() - 2);
    }

    if(departure && rightAlt->geometry.size() >= 3)
      // Right one is a TOD or TOC take the geometry at the bend
      rightPt = rightAlt->geometry.at(1);

    // Create a direct line from left to right ignoring middle leg
    QLineF line(leftPt, rightPt);

    // Calculate altitude for the middle leg using the direct line
    QPointF midPt = midAlt->asPoint();
    double t = (midPt.x() - leftPt.x()) / line.dx();

    // 0 = left, 1 = right
    QPointF mid = line.pointAt(t);

#ifdef DEBUG_INFORMATION
    qDebug() << Q_FUNC_INFO << "old" << midAlt->y2() << "new" << mid.y();
#endif

    // Change middle leg and adjust altitude
    midAlt->setY2(static_cast<float>(mid.y()));
    if(!midAlt->isEmpty())
      midAlt->setY2(adjustAltitudeForRestriction(midAlt->y2(), restriction));

    // Also change skipped right neighbor
    if(rightSkippedAlt != nullptr)
      rightSkippedAlt->setAlt(midAlt->y2());

    rightAlt->setY1(midAlt->y2());

    // Also change skipped left neighbor
    if(leftSkippedAlt != nullptr)
      leftSkippedAlt->setY2(midAlt->y2());
  }
}

void RouteAltitude::calculate()
{
  clearAll();

  if(route->size() > 1)
  {
    if(route->getTotalDistance() < 0.5f)
      return;

    // Prefill all legs with distance and cruise altitude
    calculateDistances();

    if(calcTopOfClimb)
      calculateDeparture();

    if(calcTopOfDescent)
      calculateArrival();

    // Check for violations because of too low cruise
    violatesRestrictions = false;
    for(RouteAltitudeLeg& leg : *this)
      violatesRestrictions |= violatesAltitudeRestriction(leg);

    if(violatesRestrictions || distanceTopOfClimb > distanceTopOfDescent ||
       (calcTopOfClimb && !(distanceTopOfClimb < map::INVALID_INDEX_VALUE)) ||
       (calcTopOfDescent && !(distanceTopOfDescent < map::INVALID_INDEX_VALUE)))
    {
      // TOD and TOC overlap or are invalid or restrictions violated  - cruise altitude is too high
      clearAll();

      // Reset all to cruise level - profile will print a message
      calculateDistances();
    }
    else
    {
      // Success - flatten legs
      if(simplify && (calcTopOfClimb || calcTopOfDescent))
        simplyfyRouteAltitudes();

      // Fetch ILS and VASI at destination
      calculateApproachIlsAndSlopes();
    }

#ifdef DEBUG_INFORMATION
    qDebug() << Q_FUNC_INFO << *this;
#endif
  }
}

void RouteAltitude::calculateDistances()
{
  float distanceToLeg = 0.f;
  int destinationLegIdx = route->getDestinationLegIndex();

  if(destinationLegIdx == map::INVALID_INDEX_VALUE)
  {
    qWarning() << Q_FUNC_INFO;
    return;
  }

  // Fill all legs with distance and cruise altitude and add them to the vector
  for(int i = 0; i < route->size(); i++)
  {
    const RouteLeg& leg = route->at(i);

    RouteAltitudeLeg alt;
    alt.ident = leg.getIdent();

    if(i <= destinationLegIdx)
    {
      // Not a dummy (missed)
      alt.restriction = leg.getProcedureLegAltRestr();
      alt.geometry.append(QPointF(distanceToLeg, cruiseAltitide));
      distanceToLeg += leg.getDistanceTo();
      alt.geometry.append(QPointF(distanceToLeg, cruiseAltitide));
    }
    // else ignore missed approach dummy legs after destination runway

    append(alt);
  }

  // Set the flags which are needed for drawing
  for(int i = 1; i < route->size(); i++)
  {
    const RouteLeg& leg = route->at(i);
    const RouteLeg& last = route->at(i - 1);
    RouteAltitudeLeg& altLeg = (*this)[i];

    altLeg.missed = leg.isAnyProcedure() && leg.getProcedureLeg().isMissed();

    if(last.isRoute() || leg.isRoute() || // Any is route - also covers STAR to airport
       (last.getProcedureLeg().isAnyDeparture() && leg.getProcedureLeg().isAnyArrival()) || // empty space from SID to STAR, transition or approach
       (last.getProcedureLeg().isStar() && leg.getProcedureLeg().isArrival())) // empty space from STAR to transition or approach
      altLeg.procedure = false;
    else
      altLeg.procedure = true;
  }
}

void RouteAltitude::calculateDeparture()
{
  int departureLegIdx = route->getDepartureLegIndex();
  if(departureLegIdx == map::INVALID_INDEX_VALUE)
  {
    qWarning() << Q_FUNC_INFO << "departureLegIdx" << departureLegIdx;
    return;
  }

  if(climbRateFtPerNm < 1.f)
  {
    qWarning() << Q_FUNC_INFO << "climbRateFtPerNm " << climbRateFtPerNm;
    return;
  }

  if(departureLegIdx > 0) // Assign altitude to dummy for departure airport too
    first().setAlt(departureAltitude());

  // Start from departure forward until hitting cruise altitude (TOC)
  for(int i = departureLegIdx; i < route->size(); i++)
  {
    // Calculate altitude for this leg based on altitude of the leg to the right
    RouteAltitudeLeg& alt = (*this)[i];
    if(alt.isEmpty())
      // Dummy leg
      continue;

    // Altitude of last leg
    float lastLegAlt = i > departureLegIdx ? at(i - 1).y2() : 0.;

    if(i <= departureLegIdx)
      // Departure leg - assign altitude to airport and RW too if available
      alt.setAlt(departureAltitude());
    else
    {
      // Set geometry for climbing
      alt.setY1(lastLegAlt);
      // Use a default value of 3 nm per 1000 ft if performance is not available
      alt.setY2(lastLegAlt + alt.getDistanceTo() * climbRateFtPerNm);
    }

    if(!alt.isEmpty())
    {
      // Remember geometry which is not changed by altitude restrictions for calculation of cruise intersection
      float uncorrectedAlt = alt.y2();

      adjustAltitudeForRestriction(alt);

      // Avoid climbing above any below/at/between restrictions
      float maxAlt = findDepartureMaxAltitude(i);
      if(maxAlt < map::INVALID_ALTITUDE_VALUE)
        alt.setY2(std::min(alt.y2(), maxAlt));

      // Never lower than last leg
      alt.setY2(std::max(alt.y2(), lastLegAlt));

      if(i > 0 && uncorrectedAlt > cruiseAltitide && !(distanceTopOfClimb < map::INVALID_ALTITUDE_VALUE))
      {
        // Reached TOC - calculate distance
        distanceTopOfClimb = distanceForAltitude(alt.geometry.first(), QPointF(alt.geometry.last().x(), uncorrectedAlt),
                                                 cruiseAltitide);
        legIndexTopOfClimb = i;

        // Adjust this leg
        alt.setY2(std::min(alt.y2(), cruiseAltitide));
        alt.setY2(std::max(alt.y2(), lastLegAlt));

        // Add point to allow drawing the bend at TOC
        alt.geometry.insert(1, QPointF(distanceTopOfClimb, cruiseAltitide));

        // Done here
        alt.topOfClimb = true;
        break;
      }
    }

    // Adjust altitude to be above last and below cruise
    alt.setY2(std::min(alt.y2(), cruiseAltitide));
    alt.setY2(std::max(alt.y2(), lastLegAlt));
  }
}

void RouteAltitude::calculateArrival()
{
  int destinationLegIdx = route->getDestinationLegIndex();
  int departureLegIndex = route->getDepartureLegIndex();
  float lastAlt = getDestinationAltitude();

  if(departureLegIndex == map::INVALID_INDEX_VALUE || destinationLegIdx == map::INVALID_INDEX_VALUE)
  {
    qWarning() << Q_FUNC_INFO << "departureLegIdx" << departureLegIndex << "destinationLegIdx" << destinationLegIdx;
    return;
  }

  if(desentRateFtPerNm < 1.f)
  {
    qWarning() << Q_FUNC_INFO << "climbRateFtPerNm " << desentRateFtPerNm;
    return;
  }

  // Calculate from last leg down until we hit the cruise altitude (TOD)
  for(int i = route->size() - 1; i >= 0; i--)
  {
    RouteAltitudeLeg& alt = (*this)[i];
    RouteAltitudeLeg *lastAltLeg = i < destinationLegIdx ? &(*this)[i + 1] : nullptr;
    float lastLegAlt = lastAltLeg != nullptr ? lastAltLeg->y2() : 0.;

    // Start with altitude of the right leg (close to dest)
    float newAltitude = lastLegAlt;

    if(i <= destinationLegIdx)
    {
      // Leg leading to this point (right to this)
      float distFromRight = lastAltLeg != nullptr ? lastAltLeg->getDistanceTo() : 0.f;

      // Point of this leg
      // Use a default value of 3 nm per 1000 ft if performance is not available
      newAltitude = lastAlt + distFromRight * desentRateFtPerNm;
    }

    if(!alt.isEmpty())
    {
      // Remember geometry which is not changed by altitude restrictions for calculation of cruise intersection
      float uncorrectedAltitude = newAltitude;

      // Not a dummy leg
      newAltitude = adjustAltitudeForRestriction(newAltitude, alt.restriction);

      // Avoid climbing (up the descent slope) above any below/at/between restrictions
      float maxAlt = findApproachMaxAltitude(i + 1);
      if(maxAlt < map::INVALID_ALTITUDE_VALUE)
        newAltitude = std::min(newAltitude, maxAlt);

      // Never lower than right leg
      newAltitude = std::max(newAltitude, lastAlt);

      // Check the limits to the left - either cruise or an altitude restriction
      // Set to true if an altitude restriction is limiting and postone calculation of the TOD
      bool noTod = maxAlt < map::INVALID_ALTITUDE_VALUE && maxAlt < cruiseAltitide;

      if(!noTod && !(distanceTopOfDescent < map::INVALID_ALTITUDE_VALUE) && uncorrectedAltitude > cruiseAltitide &&
         i + 1 < size())
      {
        if(at(i + 1).isEmpty())
          // Stepped into the dummies after arrival runway - bail out
          break;

        // Reached TOD - calculate distance
        distanceTopOfDescent = distanceForAltitude(at(i + 1).getGeometry().last(),
                                                   QPointF(
                                                     alt.getDistanceFromStart(), uncorrectedAltitude), cruiseAltitide);
        legIndexTopOfDescent = i + 1;

        if(!lastAltLeg->topOfClimb)
          // Adjust only if not modified by TOC calculation
          alt.setY2(std::min(newAltitude, cruiseAltitide));

        if(lastAltLeg != nullptr)
          // Adjust neighbor too
          lastAltLeg->setY1(alt.y2());

        // Append point to allow drawing the bend at TOD - TOC position might be already included in leg
        lastAltLeg->geometry.insert(lastAltLeg->geometry.size() - 1,
                                    QPointF(distanceTopOfDescent, cruiseAltitide));

        // Done here
        if(legIndexTopOfDescent < size())
          (*this)[legIndexTopOfDescent].topOfDescent = true;
        break;
      }

      // Adjust altitude to be above last and below cruise
      alt.setY2(newAltitude);
      alt.setY2(std::min(alt.y2(), cruiseAltitide));

      if(lastAltLeg != nullptr)
        alt.setY2(std::max(alt.y2(), lastLegAlt));
      if(i == destinationLegIdx && i != departureLegIndex && !alt.topOfClimb)
        alt.setY1(alt.y2());

      if(lastAltLeg != nullptr)
        lastAltLeg->setY1(alt.y2());

      lastAlt = alt.y2();
    }
  }
}

void RouteAltitude::calculateApproachIlsAndSlopes()
{
  route->getApproachRunwayEndAndIls(destRunwayIls, destRunwayEnd);

  // Filter out unusable ILS
  auto it = std::remove_if(destRunwayIls.begin(), destRunwayIls.end(),
                           [ = ](const map::MapIls& ils) -> bool
  {
    // Needs to have GS, not farther away from runway end than 4NM and not more than 20 degree difference
    return ils.slope < 0.1f || destRunwayEnd.position.distanceMeterTo(ils.position) > atools::geo::nmToMeter(4.) ||
    atools::geo::angleAbsDiff(destRunwayEnd.heading, ils.heading) > 20.f;
  });

  if(it != destRunwayIls.end())
    destRunwayIls.erase(it, destRunwayIls.end());
}

float RouteAltitude::getDestinationAltitude() const
{
  const RouteLeg& destLeg = route->at(route->getDestinationLegIndex());
  if(destLeg.isAnyProcedure() && destLeg.getProcedureLegAltRestr().isValid())
    // Use restriction from procedure - field elevation + 50 ft
    return destLeg.getProcedureLegAltRestr().alt1;
  else if(destLeg.getAirport().isValid())
    // Airport altitude
    return destLeg.getPosition().getAltitude();

  // Other leg types (waypoint, VOR, etc.) remain on cruise
  return cruiseAltitide;
}

float RouteAltitude::getDestinationDistance() const
{
  int idx = route->getDestinationLegIndex();

  if(idx < map::INVALID_INDEX_VALUE)
    return at(idx).getDistanceFromStart();
  else
    return map::INVALID_DISTANCE_VALUE;
}

float RouteAltitude::departureAltitude() const
{
  const RouteLeg& startLeg = route->at(route->getDepartureLegIndex());
  if(startLeg.isAnyProcedure() && startLeg.getProcedureLegAltRestr().isValid())
    // Use restriction from procedure - field elevation + 50 ft
    return startLeg.getProcedureLegAltRestr().alt1;
  else if(startLeg.getAirport().isValid())
    // Airport altitude
    return startLeg.getPosition().getAltitude();

  // Other leg types (waypoint, VOR, etc.) remain on cruise
  return cruiseAltitide;
}

float RouteAltitude::distanceForAltitude(const QPointF& leg1, const QPointF& leg2, float altitude)
{
  QLineF curLeg(leg1, leg2);

  // 0 = left/low 1 = this/right/high
  double t = (altitude - curLeg.y1()) / curLeg.dy();

  if(t < 0. || t > 1.)
    // Straight does not touch given altitude
    return map::INVALID_DISTANCE_VALUE;
  else
    return static_cast<float>(curLeg.pointAt(t).x());
}

float RouteAltitude::distanceForAltitude(const RouteAltitudeLeg& leg, float altitude)
{
  return distanceForAltitude(leg.geometry.first(), leg.geometry.last(), altitude);
}

void RouteAltitude::calculateTrip(const atools::fs::perf::AircraftPerf& perf, float windDir, float windSpeed)
{
  travelTime = 0.f;
  averageGroundSpeed = 0.f;
  tripFuel = 0.f;
  unflyableLegs = false;

  float tocDist = getTopOfClimbDistance();
  float todDist = getTopOfDescentDistance();
  float totalDist = route->getTotalDistance();

  if(!(tocDist < map::INVALID_DISTANCE_VALUE) || !(todDist < map::INVALID_DISTANCE_VALUE))
  {
    qWarning() << Q_FUNC_INFO << "tocDist" << tocDist << "todDist" << todDist;
    return;
  }

  for(int i = 0; i < size(); i++)
  {
    RouteAltitudeLeg& leg = (*this)[i];

    float dist = leg.getDistanceTo();
    if(atools::almostEqual(dist, 0.f))
      // same as last one - no travel time and no fuel
      continue;

    // Beginning and end of this leg
    float startDist = leg.getDistanceFromStart() - leg.getDistanceTo();
    float endDist = leg.getDistanceFromStart();

    float climbDist = 0.f, cruiseDist = 0.f, descentDist = 0.f,
          climbWindAvg = 0.f, cruiseWind = 0.f, descentWindAvg = 0.f;

    float climbSpeed = 0.f, cruiseSpeed = 0.f, descentSpeed = 0.f;

    // Check if leg covers TOC and/or TOD =================================================
    // Calculate wind, distance and averate speed (TAS) for this leg
    // Wind is interpolated by altitude
    if(endDist < tocDist)
    {
      // All climb before TOC ==========================
      climbDist = dist;
      climbWindAvg = (interpolate(0.f, windSpeed, 0.f, tocDist, startDist) +
                      interpolate(0.f, windSpeed, 0.f, tocDist, endDist)) / 2.f;
      climbSpeed = perf.getClimbSpeed();
    }
    else if(startDist > todDist)
    {
      // All descent after TOD ==========================
      descentDist = dist;
      descentWindAvg = (interpolate(windSpeed, 0.f, todDist, totalDist, startDist) +
                        interpolate(windSpeed, 0.f, todDist, totalDist, endDist)) / 2.f;
      descentSpeed = perf.getDescentSpeed();
    }
    else if(startDist < tocDist && endDist > todDist)
    {
      // Crosses TOC and TOD  - phases climb, cruise and descent ==========================
      climbDist = tocDist - startDist;
      climbWindAvg = (interpolate(0.f, windSpeed, 0.f, tocDist, startDist) + windSpeed) / 2.f;
      climbSpeed = perf.getClimbSpeed();

      cruiseDist = todDist - tocDist;
      cruiseWind = windSpeed;
      cruiseSpeed = perf.getCruiseSpeed();

      descentDist = endDist - todDist;
      descentWindAvg = (interpolate(windSpeed, 0.f, todDist, totalDist, todDist) +
                        interpolate(windSpeed, 0.f, todDist, totalDist, endDist)) / 2.f;
      descentSpeed = perf.getDescentSpeed();
    }
    else if(startDist < tocDist && endDist < todDist)
    {
      // Crosses TOC and goes into cruise ==========================
      climbDist = tocDist - startDist;
      climbWindAvg = (interpolate(0.f, windSpeed, 0.f, tocDist, startDist) + windSpeed) / 2.f;
      climbSpeed = perf.getClimbSpeed();

      cruiseDist = endDist - tocDist;
      cruiseWind = windSpeed;
      cruiseSpeed = perf.getCruiseSpeed();
    }
    else if(startDist > tocDist && endDist > todDist)
    {
      // Goes from cruise to and after TOD ==========================
      cruiseDist = todDist - startDist;
      cruiseWind = windSpeed;
      cruiseSpeed = perf.getCruiseSpeed();

      descentDist = endDist - todDist;
      descentWindAvg = (windSpeed + interpolate(windSpeed, 0.f, todDist, totalDist, endDist)) / 2.f;
      descentSpeed = perf.getDescentSpeed();
    }
    else
    {
      // Cruise only ==========================
      cruiseDist = dist;
      cruiseWind = windSpeed;
      cruiseSpeed = perf.getCruiseSpeed();
    }

    // Calculate ground speed for each phase (climb, cruise, descent) of this leg - 0 is phase is not touched
    float course = route->at(i).getCourseToMag();
    climbSpeed = atools::geo::windCorrectedGroundSpeed(climbWindAvg, windDir, course, climbSpeed);
    cruiseSpeed = atools::geo::windCorrectedGroundSpeed(cruiseWind, windDir, course, cruiseSpeed);
    descentSpeed = atools::geo::windCorrectedGroundSpeed(descentWindAvg, windDir, course, descentSpeed);

    // Check if wind is too strong
    if(!(climbSpeed < map::INVALID_SPEED_VALUE))
    {
      unflyableLegs = true;
      climbSpeed = perf.getClimbSpeed();
    }
    if(!(cruiseSpeed < map::INVALID_SPEED_VALUE))
    {
      unflyableLegs = true;
      cruiseSpeed = perf.getCruiseSpeed();
    }
    if(!(descentSpeed < map::INVALID_SPEED_VALUE))
    {
      unflyableLegs = true;
      descentSpeed = perf.getDescentSpeed();
    }

    if(atools::almostNotEqual(climbDist + cruiseDist + descentDist, dist))
      qWarning() << Q_FUNC_INFO << "Distance differs";

    // Calculate leg time ================================================================
    float climbTime = climbSpeed > 0.f ? (climbDist / climbSpeed) : 0.f;
    float cruiseTime = cruiseSpeed > 0.f ? (cruiseDist / cruiseSpeed) : 0.f;
    float descentTime = descentSpeed > 0.f ? (descentDist / descentSpeed) : 0.f;

    // Assign values to leg =========================================================
    if(!leg.isMissed() && dist < map::INVALID_DISTANCE_VALUE)
    {
      // Assign average
      leg.averageSpeedKts = (climbSpeed * climbDist +
                             cruiseSpeed * cruiseDist +
                             descentSpeed * descentDist) / dist;

      // Sum up fuel for phases
      leg.fuel = perf.getClimbFuelFlow() * climbTime +
                 perf.getCruiseFuelFlow() * cruiseTime +
                 perf.getDescentFuelFlow() * descentTime;

      // Sum up time for phases
      leg.travelTimeHours = climbTime + cruiseTime + descentTime;

      travelTime += leg.travelTimeHours;
      tripFuel += leg.fuel;

      if(atools::almostEqual(averageGroundSpeed, 0.f))
        averageGroundSpeed = leg.averageSpeedKts;
      else
        averageGroundSpeed = (leg.averageSpeedKts + averageGroundSpeed) / 2.f;
    }
  }

#ifdef DEBUG_INFORMATION
  qDebug() << Q_FUNC_INFO << *this;
#endif
}

QDebug operator<<(QDebug out, const RouteAltitude& obj)
{
  out << "TOC dist" << obj.getTopOfClimbDistance()
      << "index" << obj.getTopOfClimbLegIndex()
      << "TOD dist" << obj.getTopOfDescentDistance()
      << "index" << obj.getTopOfDescentLegIndex()
      << "travelTime " << obj.getTravelTimeHours()
      << "averageSpeed" << obj.getAverageGroundSpeed()
      << "tripFuel" << obj.getTripFuel()
      << "totalDistance" << obj.route->getTotalDistance()
      << endl;

  for(int i = 0; i < obj.size(); i++)
    out << i << obj.at(i) << endl;
  return out;
}
