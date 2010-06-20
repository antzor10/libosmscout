#ifndef OSMSCOUT_MAP_MAPPAINTER_H
#define OSMSCOUT_MAP_MAPPAINTER_H

/*
  This source is part of the libosmscout-map library
  Copyright (C) 2009  Tim Teulings

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
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307  USA
*/

#include <set>

#include <osmscout/Private/MapImportExport.h>

#include <osmscout/Projection.h>
#include <osmscout/Node.h>
#include <osmscout/Relation.h>
#include <osmscout/StyleConfig.h>
#include <osmscout/Way.h>

namespace osmscout {

  class OSMSCOUT_MAP_API MapParameter
  {
  public:
    MapParameter();
    virtual ~MapParameter();
  };

  struct OSMSCOUT_MAP_API MapData
  {
    std::vector<Node>     nodes;
    std::vector<Way>      ways;
    std::vector<Way>      areas;
    std::vector<Relation> relationWays;
    std::vector<Relation> relationAreas;
    std::list<Way>        poiWays;
    std::list<Node>       poiNodes;
  };

  class OSMSCOUT_MAP_API MapPainter
  {
  protected:
    std::vector<bool>   drawNode;     //! This nodes will be drawn
    std::vector<double> nodeX;        //! static scratch buffer for calculation
    std::vector<double> nodeY;        //! static scratch buffer for calculation

  protected:
    bool IsVisible(const Projection& projection,
                   const std::vector<Point>& nodes) const;

    void TransformArea(const Projection& projection,
                       const std::vector<Point>& nodes);
    void TransformWay(const Projection& projection,
                      const std::vector<Point>& nodes);

    bool GetBoundingBox(const std::vector<Point>& nodes,
                        double& xmin, double& ymin,
                        double& xmax, double& ymax);
    bool GetCenterPixel(const Projection& projection,
                        const std::vector<Point>& nodes,
                        double& cx,
                        double& cy);
  
  public:
    MapPainter();
    virtual ~MapPainter();
  };
}

#endif
