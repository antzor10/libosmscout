/*
  This source is part of the libosmscout library
  Copyright (C) 2010  Tim Teulings

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

#include <osmscout/import/GenWaterIndex.h>

#include <cassert>
#include <vector>

#include <osmscout/Point.h>
#include <osmscout/Relation.h>
#include <osmscout/Way.h>

#include <osmscout/util/File.h>
#include <osmscout/util/FileScanner.h>
#include <osmscout/util/String.h>

#include <osmscout/private/Math.h>

#include <iostream>

//#define DEBUG_COASTLINE
//#define DEBUG_TILING

namespace osmscout {

  /**
   * Sets the size of the bitmap and initializes state of all tiles to "unknown"
   */
  void WaterIndexGenerator::Level::SetBox(uint32_t minLat, uint32_t maxLat,
                                          uint32_t minLon, uint32_t maxLon,
                                          double cellWidth, double cellHeight)
  {
    this->cellWidth=cellWidth;
    this->cellHeight=cellHeight;

    // Convert to the real double values
    this->minLat=minLat/conversionFactor-90.0;
    this->maxLat=maxLat/conversionFactor-90.0;
    this->minLon=minLon/conversionFactor-180.0;
    this->maxLon=maxLon/conversionFactor-180.0;

    cellXStart=(uint32_t)floor((this->minLon+180.0)/cellWidth);
    cellXEnd=(uint32_t)floor((this->maxLon+180.0)/cellWidth);
    cellYStart=(uint32_t)floor((this->minLat+90.0)/cellHeight);
    cellYEnd=(uint32_t)floor((this->maxLat+90.0)/cellHeight);

    cellXCount=cellXEnd-cellXStart+1;
    cellYCount=cellYEnd-cellYStart+1;

    uint32_t size=cellXCount*cellYCount/4;

    if (cellXCount*cellYCount%4>0) {
      size++;
    }

    area.resize(size,0x00);
  }

  bool WaterIndexGenerator::Level::IsIn(uint32_t x, uint32_t y) const
  {
    return x>=cellXStart &&
           x<=cellXEnd &&
           y>=cellYStart &&
           y<=cellYEnd;
  }

  WaterIndexGenerator::State WaterIndexGenerator::Level::GetState(uint32_t x, uint32_t y) const
  {
    uint32_t cellId=y*cellXCount+x;
    uint32_t index=cellId/4;
    uint32_t offset=2*(cellId%4);

    return (State)((area[index] >> offset) & 3);
  }

  void WaterIndexGenerator::Level::SetState(uint32_t x, uint32_t y, State state)
  {
    uint32_t cellId=y*cellXCount+x;
    uint32_t index=cellId/4;
    uint32_t offset=2*(cellId%4);

    area[index]=(area[index] & ~(3 << offset));
    area[index]=(area[index] | (state << offset));
  }

  void WaterIndexGenerator::Level::SetStateAbsolute(uint32_t x, uint32_t y, State state)
  {
    SetState(x-cellXStart,y-cellYStart,state);
  }

  bool WaterIndexGenerator::LoadCoastlines(const ImportParameter& parameter,
                                           Progress& progress,
                                           const TypeConfig& typeConfig)
  {
    // We must have coastline type defined
    TypeId      coastlineWayId=typeConfig.GetWayTypeId("natural_coastline");
    TypeId      coastlineAreaId=typeConfig.GetAreaTypeId("natural_coastline");
    FileScanner scanner;
    uint32_t    wayCount=0;

    progress.SetAction("Scanning for coastlines ways");

    if (!scanner.Open(AppendFileToDir(parameter.GetDestinationDirectory(),
                                      "ways.dat"))) {
      progress.Error("Cannot open 'ways.dat'");
      return false;
    }

    if (!scanner.Read(wayCount)) {
      progress.Error("Error while reading number of data entries in file");
      return false;
    }

    for (uint32_t w=1; w<=wayCount; w++) {
      progress.SetProgress(w,wayCount);

      Way way;

      if (!way.Read(scanner)) {
        progress.Error(std::string("Error while reading data entry ")+
                       NumberToString(w)+" of "+
                       NumberToString(wayCount)+
                       " in file '"+
                       scanner.GetFilename()+"'");
        return false;
      }

      if (way.GetType()!=coastlineWayId &&
          way.GetType()!=coastlineAreaId) {
        continue;
      }

      CoastRef coast=new Coast();

      coast->id=way.GetId();
      coast->coast=way.nodes;

      coastlines.push_back(coast);
    }

    if (!scanner.Close()) {
      progress.Error("Error while reading/closing 'ways.dat'");
      return false;
    }

    return true;
  }

  void WaterIndexGenerator::MergeCoastlines(Progress& progress)
  {
    progress.SetAction("Merging coastlines");

    progress.Info("Initial coastline count: "+NumberToString(coastlines.size()));

    std::map<Id,CoastRef> coastStartMap;
    std::list<CoastRef>   mergedCoastlines;
    std::set<Id>          blacklist;

    std::list<CoastRef>::iterator c=coastlines.begin();
    while( c!=coastlines.end()) {
      CoastRef coast=*c;

      if (coast->coast.front().GetId()!=coast->coast.back().GetId()) {
        coastStartMap.insert(std::make_pair(coast->coast.front().GetId(),coast));

        c++;
      }
      else {
        mergedCoastlines.push_back(coast);

        c=coastlines.erase(c);
      }
    }

    bool merged=true;

    while (merged) {
      merged=false;

      for (std::list<CoastRef>::iterator c=coastlines.begin();
          c!=coastlines.end();
          ++c) {
        if (blacklist.find((*c)->id)!=blacklist.end()) {
          continue;
        }

        CoastRef coast=*c;

        std::map<Id,CoastRef>::iterator other=coastStartMap.find(coast->coast.back().GetId());

        if (other!=coastStartMap.end() &&
            blacklist.find(other->second->id)==blacklist.end() &&
            coast->id!=other->second->id) {
          for (size_t i=1; i<other->second->coast.size(); i++) {
            coast->coast.push_back(other->second->coast[i]);
          }
          other->second->coast.clear();

          coastStartMap.erase(other);
          blacklist.insert(other->second->id);

          merged=true;
        }
      }
    }

    // Gather merged coastlines
    for (std::list<CoastRef>::iterator c=coastlines.begin();
        c!=coastlines.end();
        ++c) {
      if (blacklist.find((*c)->id)!=blacklist.end()) {
        continue;
      }

      mergedCoastlines.push_back(*c);
    }

    progress.Info("Final coastline count: "+NumberToString(mergedCoastlines.size()));

    coastlines=mergedCoastlines;
  }


  /**
   * Does a scan line conversion on all costline ways where the cell size equals the tile size.
   * All tiles hit are marked as "coast".
   *
   */
  void WaterIndexGenerator::MarkCoastlineCells(Progress& progress,
                                               Level& level)
  {
    progress.Info("Setting coastline cells");

    for (std::list<CoastRef>::const_iterator c=coastlines.begin();
        c!=coastlines.end();
        ++c) {
      const CoastRef& coastline=*c;

      // Marks cells on the path as coast

      std::vector<ScanCell> cells;

      ScanConvertLine(coastline->coast,-180.0,level.cellWidth,-90.0,level.cellHeight,cells);

      for (size_t i=0; i<cells.size(); i++) {
        if (level.IsIn(cells[i].x,cells[i].y)) {
          if (level.GetState(cells[i].x-level.cellXStart,cells[i].y-level.cellYStart)==unknown) {
#if defined(DEBUG_TILING)
            std::cout << "Coastline: " << cells[i].x-level.cellXStart << "," << cells[i].y-level.cellYStart << std::endl;
#endif
            level.SetStateAbsolute(cells[i].x,cells[i].y,coast);
          }
        }
      }
    }
  }

  /**
   * We assume that coastlines do not intersect each other. We split each coastline into smaller line segments.
   * We sort the segments first by minimum longitude and then by slope. We than assume that left of each
   * line is either water or land, depending on the slope direction. We then resort from right to left and do
   * the estimation again.
   */
  void WaterIndexGenerator::ScanCellsHorizontally(Progress& progress,
                                                  Level& level)
  {
    progress.Info("Scan coastlines horizontally");

    std::list<Line> lines;

    for (std::list<CoastRef>::const_iterator c=coastlines.begin();
        c!=coastlines.end();
        ++c) {
      const CoastRef& coastline=*c;

      for (size_t i=0; i<coastline->coast.size()-1; i++) {
        Line line;

        line.a=coastline->coast[i];
        line.b=coastline->coast[i+1];
        lines.push_back(line);
      }
    }

    for (std::list<Line>::iterator line=lines.begin();
        line!=lines.end();
        ++line) {
      line->sortCriteria1=std::min(line->a.GetLon(),line->b.GetLon());
      line->sortCriteria2=std::abs((line->b.GetLat()-line->a.GetLat())/(line->b.GetLon()-line->a.GetLon()));
    }

    lines.sort(SmallerLineSort());

    for (std::list<Line>::const_iterator line=lines.begin();
        line!=lines.end();
        ++line) {
      std::vector<ScanCell> cells;

      ScanConvertLine((int)((line->a.GetLon()+180.0)/level.cellWidth),
                      (int)((line->a.GetLat()+90.0)/level.cellHeight),
                      (int)((line->b.GetLon()+180.0)/level.cellWidth),
                      (int)((line->b.GetLat()+90.0)/level.cellHeight),
                      cells);

      if (line->b.GetLat()>line->a.GetLat()) {
        // up
        for (size_t i=0; i<cells.size(); i++) {
          if (level.IsIn(cells[i].x,cells[i].y)) {
            int cx=cells[i].x-level.cellXStart;
            int cy=cells[i].y-level.cellYStart;

            if (cx-1>=0 && level.GetState(cx-1,cy)==unknown) {
#if defined(DEBUG_TILING)
              std::cout << "Land beneath coast: " << cx-1 << "," << cy << std::endl;
#endif
              level.SetState(cx-1,cy,land);
            }
          }
        }
      }
      else if (line->b.GetLat()<line->a.GetLat()) {
        // down
        for (size_t i=0; i<cells.size(); i++) {
          if (level.IsIn(cells[i].x,cells[i].y)) {
            int cx=cells[i].x-level.cellXStart;
            int cy=cells[i].y-level.cellYStart;

            if (cx-1>=0 && level.GetState(cx-1,cy)==unknown) {
#if defined(DEBUG_TILING)
              std::cout << "Water beneath coast: " << cx-1 << "," << cy << std::endl;
#endif
              level.SetState(cx-1,cy,water);
            }
          }
        }
      }
    }

    for (std::list<Line>::iterator line=lines.begin();
        line!=lines.end();
        ++line) {
      line->sortCriteria1=std::max(line->a.GetLon(),line->b.GetLon());
      line->sortCriteria2=std::abs((line->b.GetLat()-line->a.GetLat())/(line->b.GetLon()-line->a.GetLon()));
    }

    lines.sort(BiggerLineSort());

    for (std::list<Line>::const_iterator line=lines.begin();
        line!=lines.end();
        ++line) {
      std::vector<ScanCell> cells;

      ScanConvertLine((int)((line->a.GetLon()+180.0)/level.cellWidth),
                      (int)((line->a.GetLat()+90.0)/level.cellHeight),
                      (int)((line->b.GetLon()+180.0)/level.cellWidth),
                      (int)((line->b.GetLat()+90.0)/level.cellHeight),
                      cells);
      if (line->b.GetLat()>line->a.GetLat()) {
        // up
        for (size_t i=0; i<cells.size(); i++) {
          if (level.IsIn(cells[i].x,cells[i].y)) {
            int cx=cells[i].x-level.cellXStart;
            int cy=cells[i].y-level.cellYStart;

            if (cx+1<level.cellXCount && level.GetState(cx+1,cy)==unknown) {
#if defined(DEBUG_TILING)
              std::cout << "Water beneath coast: " << cx+1 << "," << cy << std::endl;
#endif
              level.SetState(cx+1,cy,water);
            }
          }
        }
      }
      else if (line->b.GetLat()<line->a.GetLat()) {
        // down
        for (size_t i=0; i<cells.size(); i++) {
          if (level.IsIn(cells[i].x,cells[i].y)) {
            int cx=cells[i].x-level.cellXStart;
            int cy=cells[i].y-level.cellYStart;

            if (cx+1<level.cellXCount && level.GetState(cx+1,cy)==unknown) {
#if defined(DEBUG_TILING)
              std::cout << "Land beneath coast: " << cx+1 << "," << cy << std::endl;
#endif
              level.SetState(cx+1,cy,land);
            }
          }
        }
      }
    }
  }

  /**
   * See description for ScanCellsHorizontally()
   */
  void WaterIndexGenerator::ScanCellsVertically(Progress& progress,
                                                Level& level)
  {
    progress.Info("Scan coastlines vertically");

    std::list<Line> lines;

    for (std::list<CoastRef>::const_iterator c=coastlines.begin();
        c!=coastlines.end();
        ++c) {
      const CoastRef& coastline=*c;

      for (size_t i=0; i<coastline->coast.size()-1; i++) {
        Line line;

        line.a=coastline->coast[i];
        line.b=coastline->coast[i+1];
        lines.push_back(line);
      }
    }

    for (std::list<Line>::iterator line=lines.begin();
        line!=lines.end();
        ++line) {
      line->sortCriteria1=std::min(line->a.GetLat(),line->b.GetLat());
      line->sortCriteria2=std::abs((line->b.GetLon()-line->a.GetLon())/(line->b.GetLat()-line->a.GetLat()));
    }

    lines.sort(SmallerLineSort());

    for (std::list<Line>::const_iterator line=lines.begin();
        line!=lines.end();
        ++line) {
      std::vector<ScanCell> cells;

      ScanConvertLine((int)((line->a.GetLon()+180.0)/level.cellWidth),
                      (int)((line->a.GetLat()+90.0)/level.cellHeight),
                      (int)((line->b.GetLon()+180.0)/level.cellWidth),
                      (int)((line->b.GetLat()+90.0)/level.cellHeight),
                      cells);

      if (line->b.GetLon()>line->a.GetLon()) {
        // right
        for (size_t i=0; i<cells.size(); i++) {
          if (level.IsIn(cells[i].x,cells[i].y)) {
            int cx=cells[i].x-level.cellXStart;
            int cy=cells[i].y-level.cellYStart;

            if (cy-1>=0 && level.GetState(cx,cy-1)==unknown) {
#if defined(DEBUG_TILING)
              std::cout << "Water beneath coast: " << cx << "," << cy-1 << std::endl;
#endif
              level.SetState(cx,cy-1,water);
            }
          }
        }
      }
      else if (line->b.GetLon()<line->a.GetLon()) {
        // left
        for (size_t i=0; i<cells.size(); i++) {
          if (level.IsIn(cells[i].x,cells[i].y)) {
            int cx=cells[i].x-level.cellXStart;
            int cy=cells[i].y-level.cellYStart;

            if (cy-1>=0 && level.GetState(cx,cy-1)==unknown) {
#if defined(DEBUG_TILING)
              std::cout << "Land beneath coast: " << cx << "," << cy-1 << std::endl;
#endif
              level.SetState(cx,cy-1,land);
            }
          }
        }
      }
    }

    for (std::list<Line>::iterator line=lines.begin();
        line!=lines.end();
        ++line) {
      line->sortCriteria1=std::max(line->a.GetLat(),line->b.GetLat());
      line->sortCriteria2=std::abs((line->b.GetLon()-line->a.GetLon())/(line->b.GetLat()-line->a.GetLat()));
    }

    lines.sort(BiggerLineSort());

    for (std::list<Line>::const_iterator line=lines.begin();
        line!=lines.end();
        ++line) {
      std::vector<ScanCell> cells;

      ScanConvertLine((int)((line->a.GetLon()+180.0)/level.cellWidth),
                      (int)((line->a.GetLat()+90.0)/level.cellHeight),
                      (int)((line->b.GetLon()+180.0)/level.cellWidth),
                      (int)((line->b.GetLat()+90.0)/level.cellHeight),
                      cells);
      if (line->b.GetLon()>line->a.GetLon()) {
        // right
        for (size_t i=0; i<cells.size(); i++) {
          if (level.IsIn(cells[i].x,cells[i].y)) {
            int cx=cells[i].x-level.cellXStart;
            int cy=cells[i].y-level.cellYStart;

            if (cy+1<level.cellYCount && level.GetState(cx,cy+1)==unknown) {
#if defined(DEBUG_TILING)
              std::cout << "Land beneath coast: " << cx << "," << cy+1 << std::endl;
#endif
              level.SetState(cx,cy+1,land);
            }
          }
        }
      }
      else if (line->b.GetLon()<line->a.GetLon()) {
        // left
        for (size_t i=0; i<cells.size(); i++) {
          if (level.IsIn(cells[i].x,cells[i].y)) {
            int cx=cells[i].x-level.cellXStart;
            int cy=cells[i].y-level.cellYStart;

            if (cy+1<level.cellYCount && level.GetState(cx,cy+1)==unknown) {
#if defined(DEBUG_TILING)
              std::cout << "Water beneath coast: " << cx << "," << cy+1 << std::endl;
#endif
              level.SetState(cx,cy+1,water);
            }
          }
        }
      }
    }
  }

  /**
   * Every tile that is unknown but contains a way, must be land.
   */
  bool WaterIndexGenerator::AssumeLand(const ImportParameter& parameter,
                                       Progress& progress,
                                       const TypeConfig& typeConfig,
                                       Level& level)
  {
    progress.Info("Assume land");

    FileScanner scanner;

    TypeId      coastlineWayId=typeConfig.GetWayTypeId("natural_coastline");
    uint32_t    wayCount=0;

    assert(coastlineWayId!=typeIgnore);

    // We do not yet know if we handle borders as ways or areas

    if (!scanner.Open(AppendFileToDir(parameter.GetDestinationDirectory(),
                                      "ways.dat"))) {
      progress.Error("Cannot open 'ways.dat'");
      return false;
    }

    if (!scanner.Read(wayCount)) {
      progress.Error("Error while reading number of data entries in file");
      return false;
    }

    for (uint32_t w=1; w<=wayCount; w++) {
      progress.SetProgress(w,wayCount);

      Way way;

      if (!way.Read(scanner)) {
        progress.Error(std::string("Error while reading data entry ")+
                       NumberToString(w)+" of "+
                       NumberToString(wayCount)+
                       " in file '"+
                       scanner.GetFilename()+"'");
        return false;
      }

      if (way.GetType()!=coastlineWayId &&
          !typeConfig.GetTypeInfo(way.GetType()).GetIgnoreSeaLand()) {
        if (!way.IsArea() &&
            way.nodes.size()>=2) {
          std::vector<ScanCell> cells;

          ScanConvertLine(way.nodes,-180.0,level.cellWidth,-90.0,level.cellHeight,cells);

          for (size_t i=0; i<cells.size(); i++) {
            if (level.IsIn(cells[i].x,cells[i].y)) {
              if (level.GetState(cells[i].x-level.cellXStart,cells[i].y-level.cellYStart)==unknown) {
#if defined(DEBUG_TILING)
          std::cout << "Assume land: " << cells[i].x-level.cellXStart << "," << cells[i].y-level.cellYStart << " Way " << way.GetId() << " " << typeConfig.GetTypeInfo(way.GetType()).GetName() << " is defining area as land" << std::endl;
#endif
                level.SetStateAbsolute(cells[i].x,cells[i].y,land);
              }
            }
          }
        }
      }
    }

    return true;
  }

  /**
   * Scanning from left to right and bottom to top: Every tile that is unknown but is placed between land and coast or
   * land tiles must be land, too.
   */
  void WaterIndexGenerator::FillLand(Progress& progress,
                                     Level& level)
  {
    progress.Info("Filling land");

    bool cont=true;

    while (cont) {
      cont=false;

      // Left to right
      for (size_t y=0; y<level.cellYCount; y++) {
        int x=0;
        int start=0;
        int end=0;
        int state=0;

        while (x<level.cellXCount) {
          switch (state) {
            case 0:
              if (level.GetState(x,y)==land) {
                state=1;
              }
              x++;
              break;
            case 1:
              if (level.GetState(x,y)==unknown) {
                state=2;
                start=x;
                end=x;
                x++;
              }
              else {
                state=0;
              }
              break;
            case 2:
              if (level.GetState(x,y)==unknown) {
                end=x;
                x++;
              }
              else if (level.GetState(x,y)==coast || level.GetState(x,y)==land) {
                if (start<level.cellXCount && end<level.cellXCount && start<=end) {
                  for (size_t i=start; i<=end; i++) {
#if defined(DEBUG_TILING)
                    std::cout << "Land between: " << i << "," << y << std::endl;
#endif
                    level.SetState(i,y,land);
                    cont=true;
                  }
                }

                state=0;
              }
              else {
                state=0;
              }
              break;
          }
        }
      }

      //Bottom Up
      for (size_t x=0; x<level.cellXCount; x++) {
        int y=0;
        int start=0;
        int end=0;
        int state=0;

        while (y<level.cellYCount) {
          switch (state) {
            case 0:
              if (level.GetState(x,y)==land) {
                state=1;
              }
              y++;
              break;
            case 1:
              if (level.GetState(x,y)==unknown) {
                state=2;
                start=y;
                end=y;
                y++;
              }
              else {
                state=0;
              }
              break;
            case 2:
              if (level.GetState(x,y)==unknown) {
                end=y;
                y++;
              }
              else if (level.GetState(x,y)==coast || level.GetState(x,y)==land) {
                if (start<level.cellYCount && end<level.cellYCount && start<=end) {
                  for (size_t i=start; i<=end; i++) {
#if defined(DEBUG_TILING)
                    std::cout << "Land between: " << x << "," << i << std::endl;
#endif
                    level.SetState(x,i,land);
                    cont=true;
                  }
                }

                state=0;
              }
              else {
                state=0;
              }
              break;
          }
        }
      }
    }
  }

  /**
   * Converts all tiles of state "unknown" that touch a tile with state "water" to state "water", too.
   */
  void WaterIndexGenerator::FillWater(Progress& progress,
                                      Level& level,
                                      size_t tileCount)
  {
    progress.Info("Filling water");

    for (size_t i=1; i<=tileCount; i++) {

      Level newLevel(level);

      for (size_t y=0; y<level.cellYCount; y++) {
        for (size_t x=0; x<level.cellXCount; x++) {
          if (level.GetState(x,y)==water) {
            if (y>0) {
              if (level.GetState(x,y-1)==unknown) {
#if defined(DEBUG_TILING)
                std::cout << "Water beneath water: " << x << "," << y-1 << std::endl;
#endif
                newLevel.SetState(x,y-1,water);
              }
            }

            if (y<level.cellYCount-1) {
              if (level.GetState(x,y+1)==unknown) {
#if defined(DEBUG_TILING)
                std::cout << "Water beneath water: " << x << "," << y+1 << std::endl;
#endif
                newLevel.SetState(x,y+1,water);
              }
            }

            if (x>0) {
              if (level.GetState(x-1,y)==unknown) {
#if defined(DEBUG_TILING)
                std::cout << "Water beneath water: " << x-1 << "," << y << std::endl;
#endif
                newLevel.SetState(x-1,y,water);
              }
            }

            if (x<level.cellXCount-1) {
              if (level.GetState(x+1,y)==unknown) {
#if defined(DEBUG_TILING)
                std::cout << "Water beneath water: " << x+1 << "," << y << std::endl;
#endif
                newLevel.SetState(x+1,y,water);
              }
            }
          }
        }
      }

      level=newLevel;
    }
  }

  void WaterIndexGenerator::DumpIndexHeader(const ImportParameter& parameter,
                                            FileWriter& writer,
                                            std::vector<Level>& levels)
  {
    writer.WriteNumber((uint32_t)(parameter.GetWaterIndexMinMag()));
    writer.WriteNumber((uint32_t)(parameter.GetWaterIndexMaxMag()));

    for (size_t level=0; level<levels.size(); level++) {
      FileOffset offset=0;
      writer.GetPos(levels[level].indexEntryOffset);
      writer.WriteFileOffset(offset);
      writer.WriteNumber(levels[level].cellXStart);
      writer.WriteNumber(levels[level].cellXEnd);
      writer.WriteNumber(levels[level].cellYStart);
      writer.WriteNumber(levels[level].cellYEnd);
    }
  }

  void WaterIndexGenerator::HandleAreaCoastlinesCompletelyInACell(const ImportParameter& parameter,
                                                                  Progress& progress,
                                                                  Projection& projection,
                                                                  const Level& level,
                                                                  const std::list<CoastRef>& coastlines,
                                                                  std::map<Coord,std::list<GroundTile> >& cellGroundTileMap)
  {
    progress.Info("Handle area coastline completely in a cell");

    size_t currentCoastline=0;
    for (std::list<CoastRef>::const_iterator c=coastlines.begin();
        c!=coastlines.end();
        ++c) {
      const  CoastRef& coast=*c;

      currentCoastline++;
      progress.SetProgress(currentCoastline,coastlines.size());

      if (coast->coast.front().GetId()==coast->coast.back().GetId()) {
        double   minLat,maxLat,minLon,maxLon;
        uint32_t cx1,cx2,cy1,cy2;

        minLat=coast->coast[0].GetLat();
        maxLat=minLat;
        minLon=coast->coast[0].GetLon();
        maxLon=minLon;

        for (size_t p=1; p<coast->coast.size()-1; p++) {
          minLat=std::min(minLat,coast->coast[p].GetLat());
          maxLat=std::max(maxLat,coast->coast[p].GetLat());

          minLon=std::min(minLon,coast->coast[p].GetLon());
          maxLon=std::max(maxLon,coast->coast[p].GetLon());
        }

        cx1=(uint32_t)floor((minLon+180.0)/level.cellWidth);
        cx2=(uint32_t)floor((maxLon+180.0)/level.cellWidth);
        cy1=(uint32_t)floor((minLat+90.0)/level.cellHeight);
        cy2=(uint32_t)floor((maxLat+90.0)/level.cellHeight);

        if (cx1==cx2 && cy1==cy2) {
          Coord        coord(cx1-level.cellXStart,cy1-level.cellYStart);
          TransPolygon polygon;
          double       minX;
          double       maxX;
          double       minY;
          double       maxY;
          GroundTile   groundTile(GroundTile::land);

          polygon.TransformArea(projection,parameter.GetOptimizationWayMethod(),coast->coast, 1.0);

          polygon.GetBoundingBox(minX,minY,maxX,maxY);

          if (maxX-minX<=1.0 && maxY-minY<=1.0) {
            continue;
          }

          groundTile.points.reserve(polygon.GetLength());

          for (size_t p=polygon.GetStart(); p<=polygon.GetEnd(); p++) {
            if (polygon.points[p].draw) {
              groundTile.points.push_back(coast->coast[p]);
            }
          }

          cellGroundTileMap[coord].push_back(groundTile);
        }
      }
    }
  }

  static bool IsLeftOnSameBorder(size_t border, const Point& a,const Point& b)
  {
    switch (border) {
    case 0:
      return b.GetLon()>a.GetLon();
    case 1:
      return b.GetLat()<a.GetLat();
    case 2:
      return b.GetLon()<a.GetLon();
    case 3:
      return b.GetLat()>a.GetLat();
    }

    assert(false);

    return false;
  }

  void WaterIndexGenerator::GetCellIntersections(const Level& level,
                                                 const std::vector<Point>& points,
                                                 size_t coastline,
                                                 std::map<Coord,std::list<Intersection> >& cellIntersections)
  {
    for (size_t p=0; p<points.size()-1; p++) {
      uint32_t cx1=(uint32_t)floor((points[p].GetLon()+180.0)/level.cellWidth);
      uint32_t cy1=(uint32_t)floor((points[p].GetLat()+90.0)/level.cellHeight);

      uint32_t cx2=(uint32_t)floor((points[p+1].GetLon()+180.0)/level.cellWidth);
      uint32_t cy2=(uint32_t)floor((points[p+1].GetLat()+90.0)/level.cellHeight);

      if (cx1!=cx2 || cy1!=cy2) {
        for (size_t x=std::min(cx1,cx2); x<=std::max(cx1,cx2); x++) {
          for (size_t y=std::min(cy1,cy2); y<=std::max(cy1,cy2); y++) {
            Coord              coord(x-level.cellXStart,y-level.cellYStart);
            Point              borderPoints[5];
            double             xmin,xmax,ymin,ymax;

            xmin=x*level.cellWidth-180.0;
            xmax=(x+1)*level.cellWidth-180.0;
            ymin=y*level.cellHeight-90.0;
            ymax=(y+1)*level.cellHeight-90.0;

            borderPoints[0]=Point(1,ymax,xmin); // top left
            borderPoints[1]=Point(2,ymax,xmax); // top right
            borderPoints[2]=Point(3,ymin,xmax); // bottom right
            borderPoints[3]=Point(4,ymin,xmin); // bottom left
            borderPoints[4]=borderPoints[0];    // To avoid modula 4 on all indexes

            size_t       intersectionCount=0;
            Intersection firstIntersection;
            Intersection secondIntersection;
            size_t       corner=0;

            while (corner<4) {
              if (GetLineIntersection(points[p],
                                      points[p+1],
                                      borderPoints[corner],
                                      borderPoints[corner+1],
                                      firstIntersection.point)) {
                intersectionCount++;

                firstIntersection.coastline=coastline;
                firstIntersection.prevWayPointIndex=p;
                firstIntersection.distanceSquare=DistanceSquare(points[p],firstIntersection.point);
                firstIntersection.borderIndex=corner;

                corner++;
                break;
              }

              corner++;
            }

            while (corner<4) {
              if (GetLineIntersection(points[p],
                                      points[p+1],
                                      borderPoints[corner],
                                      borderPoints[corner+1],
                                      secondIntersection.point)) {
                intersectionCount++;

                secondIntersection.coastline=coastline;
                secondIntersection.prevWayPointIndex=p;
                secondIntersection.distanceSquare=DistanceSquare(points[p],secondIntersection.point);
                secondIntersection.borderIndex=corner;

                corner++;
                break;
              }

              corner++;
            }

            if (x==cx1 && y==cy1) {
              // The segment always leaves the origin cell

              assert(intersectionCount==1);

              firstIntersection.direction=-1;
              cellIntersections[coord].push_back(firstIntersection);
            }
            else if (x==cx2 && y==cy2) {
              // The segment always enteres the detsination cell

              assert(intersectionCount==1);

              firstIntersection.direction=1;
              cellIntersections[coord].push_back(firstIntersection);
            }
            else {
              assert(intersectionCount==0 || intersectionCount==1 || intersectionCount==2);

              if (intersectionCount==1) {
                // If we only have one intersection with borders of cells between the starting borderPoints and the
                // target borderPoints then this is a "touch"
                firstIntersection.direction=0;
                cellIntersections[coord].push_back(firstIntersection);
              }
              else if (intersectionCount==2) {
                // If we have two intersections with borders of cells between the starting cell and the
                // target cell then the one closer to the starting point is the incoming and the one farer
                // away is the leaving one
                double firstLength=DistanceSquare(firstIntersection.point,points[p]);
                double secondLength=DistanceSquare(secondIntersection.point,points[p]);

                if (firstLength<=secondLength) {
                  firstIntersection.direction=1; // Enter
                  cellIntersections[coord].push_back(firstIntersection);

                  secondIntersection.direction=-1; // Leave
                  cellIntersections[coord].push_back(secondIntersection);
                }
                else {
                  secondIntersection.direction=1; // Enter
                  cellIntersections[coord].push_back(secondIntersection);

                  firstIntersection.direction=-1; // Leave
                  cellIntersections[coord].push_back(firstIntersection);

                }
              }
            }
          }
        }
      }
    }
  }

  WaterIndexGenerator::IntersectionPtr WaterIndexGenerator::GetPreviousIntersection(std::list<IntersectionPtr>& intersectionsPathOrder,
                                                                                    const IntersectionPtr& current)
  {
    std::list<IntersectionPtr>::iterator currentIter=intersectionsPathOrder.begin();

    while (currentIter!=intersectionsPathOrder.end() &&
           (*currentIter)!=current) {
      ++currentIter;
    }

    if (currentIter==intersectionsPathOrder.end()) {
      return NULL;
    }

    if (currentIter==intersectionsPathOrder.begin()) {
      return NULL;
    }

    currentIter--;

    return *currentIter;
  }

  /**
   * Closes the sling from the incoming intersection to the outgoing intersection traveling clock wise around the cell
   * border.
   */
  void WaterIndexGenerator::WalkBorderCW(GroundTile& groundTile,
                                         const IntersectionPtr& incoming,
                                         const IntersectionPtr& outgoing,
                                         const Point borderPoints[])
  {

    if (outgoing->borderIndex!=incoming->borderIndex ||
        !IsLeftOnSameBorder(incoming->borderIndex,incoming->point,outgoing->point)) {
      size_t borderPoint=(incoming->borderIndex+1)%4;
      size_t endBorderPoint=outgoing->borderIndex;

      while (borderPoint!=endBorderPoint) {
        groundTile.points.push_back(borderPoints[borderPoint]);

        if (borderPoint==3) {
          borderPoint=0;
        }
        else {
          borderPoint++;
        }
      }

      groundTile.points.push_back(borderPoints[borderPoint]);
    }

    groundTile.points.push_back(outgoing->point);
  }


  WaterIndexGenerator::IntersectionPtr WaterIndexGenerator::GetNextCW(const std::list<IntersectionPtr>& intersectionsCW,
                                                                      const IntersectionPtr& current) const
  {
    std::list<IntersectionPtr>::const_iterator next=intersectionsCW.begin();

    while (next!=intersectionsCW.end() &&
           (*next)!=current) {
      next++;
    }

    assert(next!=intersectionsCW.end());

    next++;

    if (next==intersectionsCW.end()) {
      next=intersectionsCW.begin();
    }

    assert(next!=intersectionsCW.end());

    return *next;
  }

  void WaterIndexGenerator::WalkPathBack(GroundTile& groundTile,
                                         const IntersectionPtr& outgoing,
                                         const IntersectionPtr& incoming,
                                         const std::vector<Point>& points,
                                         bool isArea)
  {
    if (isArea) {
      if (outgoing->prevWayPointIndex==incoming->prevWayPointIndex &&
          outgoing->distanceSquare>incoming->distanceSquare) {
        groundTile.points.push_back(incoming->point);
      }
      else {
        size_t idx=outgoing->prevWayPointIndex;
        size_t targetIdx=incoming->prevWayPointIndex+1;

        if (targetIdx==points.size()) {
          targetIdx=0;
        }

        while (idx!=targetIdx) {
          groundTile.points.push_back(points[idx]);

          if (idx>0) {
            idx--;
          }
          else {
            idx=points.size()-1;
          }
        }

        groundTile.points.push_back(points[idx]);

        groundTile.points.push_back(incoming->point);
      }
    }
    else {
      size_t targetIdx=incoming->prevWayPointIndex+1;

      for (int idx=outgoing->prevWayPointIndex;
          idx>=targetIdx;
          idx--) {
        groundTile.points.push_back(points[idx]);
      }

      groundTile.points.push_back(incoming->point);
    }

  }

  /**
   * The algorithm is as following:
   * TODO
   */
  void WaterIndexGenerator::HandleCoastlinesPartiallyInACell(const ImportParameter& parameter,
                                                             Progress& progress,
                                                             Projection& projection,
                                                             const Level& level,
                                                             const std::list<CoastRef>& coastlines,
                                                             std::map<Coord,std::list<GroundTile> >& cellGroundTileMap)
  {
    progress.Info("Handle coastlines partially in a cell");

    std::map<Coord,std::list<size_t> >                     cellCoastlines;
    std::vector<std::map<Coord,std::list<Intersection> > > cellIntersections;
    std::vector<bool>                                      isArea;
    std::vector<std::vector<Point> >                       points;

    cellIntersections.resize(coastlines.size());
    isArea.resize(coastlines.size());
    points.resize(coastlines.size());

    size_t currentCoastline=0;
    for (std::list<CoastRef>::const_iterator c=coastlines.begin();
        c!=coastlines.end();
        ++c) {
      const  CoastRef& coast=*c;

      progress.SetProgress(currentCoastline,coastlines.size());

      TransPolygon polygon;

      isArea[currentCoastline]=coast->coast.front().GetId()==coast->coast.back().GetId();

      if (isArea[currentCoastline]) {
        // TODO: We already handled areas that are completely within one cell, we should skip them here
        polygon.TransformArea(projection,parameter.GetOptimizationWayMethod(),coast->coast, 1.0);
      }
      else {
        polygon.TransformWay(projection,parameter.GetOptimizationWayMethod(),coast->coast, 1.0);
      }

      points.reserve(coast->coast.size());
      for (size_t p=polygon.GetStart(); p<=polygon.GetEnd(); p++) {
        if (polygon.points[p].draw) {
          points[currentCoastline].push_back(coast->coast[p]);
        }
      }

      // Currently transformation optimization code some times draw the closing points for areas
      if (isArea[currentCoastline] &&
          points[currentCoastline].front().GetId()!=points[currentCoastline].back().GetId()) {
        points[currentCoastline].push_back(points[currentCoastline].front());
      }

      // Calculate all intersections for all path steps for all cells covered
      GetCellIntersections(level,
                           points[currentCoastline],
                           currentCoastline,
                           cellIntersections[currentCoastline]);

      for (std::map<Coord,std::list<Intersection> >::iterator cell=cellIntersections[currentCoastline].begin();
          cell!=cellIntersections[currentCoastline].end();
          ++cell) {
        cellCoastlines[cell->first].push_back(currentCoastline);
      }

      currentCoastline++;

    }

    // For every cell with intersections
    size_t currentCell=0;
    for (std::map<Coord,std::list<size_t> >::const_iterator cell=cellCoastlines.begin();
         cell!=cellCoastlines.end();
        ++cell) {
      progress.SetProgress(currentCell,cellCoastlines.size());

      currentCell++;

      std::list<IntersectionPtr>               intersectionsCW;
      std::list<IntersectionPtr>               intersectionsOuter;
      std::vector<std::list<IntersectionPtr> > intersectionsPathOrder;

      intersectionsPathOrder.resize(coastlines.size());

      for (std::list<size_t>::const_iterator currentCoastline=cell->second.begin();
          currentCoastline!=cell->second.end();
          ++currentCoastline) {
        std::map<Coord,std::list<Intersection> >::iterator cellData=cellIntersections[*currentCoastline].find(cell->first);

        if (cellData==cellIntersections[*currentCoastline].end()) {
          continue;
        }

        // Build list of intersections in path order and list of intersections in clock wise order
        for (std::list<Intersection>::iterator inter=cellData->second.begin();
            inter!=cellData->second.end();
            ++inter) {
          IntersectionPtr intersection=&(*inter);

          intersectionsPathOrder[*currentCoastline].push_back(intersection);
          intersectionsCW.push_back(intersection);
        }

        intersectionsPathOrder[*currentCoastline].sort(IntersectionByPathComparator());

        // Fix intersection order for areas
        if (isArea[*currentCoastline] &&
            intersectionsPathOrder[*currentCoastline].front()->direction==-1) {
          intersectionsPathOrder[*currentCoastline].push_back(intersectionsPathOrder[*currentCoastline].front());
          intersectionsPathOrder[*currentCoastline].pop_front();
        }

        for (std::list<IntersectionPtr>::reverse_iterator inter=intersectionsPathOrder[*currentCoastline].rbegin();
            inter!=intersectionsPathOrder[*currentCoastline].rend();
            inter++) {
          if ((*inter)->direction==-1) {
            intersectionsOuter.push_back(*inter);
          }
        }
      }


      intersectionsCW.sort(IntersectionCWComparator());

      double xmin,xmax,ymin,ymax;
      Point  borderPoints[4];

      xmin=(level.cellXStart+cell->first.x)*level.cellWidth-180.0;
      xmax=(level.cellXStart+cell->first.x+1)*level.cellWidth-180.0;
      ymin=(level.cellYStart+cell->first.y)*level.cellHeight-90.0;
      ymax=(level.cellYStart+cell->first.y+1)*level.cellHeight-90.0;

      borderPoints[0]=Point(1,ymax,xmin); // top left
      borderPoints[1]=Point(2,ymax,xmax); // top right
      borderPoints[2]=Point(3,ymin,xmax); // bottom right
      borderPoints[3]=Point(4,ymin,xmin); // bottom left

#if defined(DEBUG_COASTLINE)
      std::cout << "-- Cell: " << cell->first.x << "," << cell->first.y << std::endl;

      for (size_t currentCoastline=0;
          currentCoastline<coastlines.size();
          currentCoastline++) {
        if (!intersectionsPathOrder[currentCoastline].empty()) {
          std::cout << "Coastline " << currentCoastline << std::endl;
          for (std::list<IntersectionPtr>::const_iterator iter=intersectionsPathOrder[currentCoastline].begin();
              iter!=intersectionsPathOrder[currentCoastline].end();
              ++iter) {
            IntersectionPtr intersection=*iter;
            std::cout <<"> "  << intersection->coastline << " " << points[intersection->coastline][intersection->prevWayPointIndex].GetId() << " " << intersection->prevWayPointIndex << " " << intersection->distanceSquare << " " << intersection->point.GetLat() << "," << intersection->point.GetLon() << " " << (unsigned int)intersection->borderIndex << " " << (int)intersection->direction << std::endl;
          }
        }
      }

      std::cout << "-" << std::endl;
      for (std::list<IntersectionPtr>::const_iterator iter=intersectionsCW.begin();
          iter!=intersectionsCW.end();
          ++iter) {
        IntersectionPtr intersection=*iter;
        std::cout <<"* "  << intersection->coastline << " " << points[intersection->coastline][intersection->prevWayPointIndex].GetId() << " " << (unsigned int)intersection->prevWayPointIndex << " " << intersection->distanceSquare << " " << intersection->point.GetLat() << "," << intersection->point.GetLon() << " " << (unsigned int)intersection->borderIndex << " " << (int)intersection->direction << std::endl;
      }
#endif

      while (!intersectionsOuter.empty()) {
        GroundTile      groundTile(GroundTile::land);
        IntersectionPtr initialOutgoing;

        // Take an unused outgoing intersection as far possible down the path
        initialOutgoing=intersectionsOuter.front();
        intersectionsOuter.pop_front();

#if defined(DEBUG_COASTLINE)
        std::cout << "Outgoing: " << initialOutgoing->coastline << " " << initialOutgoing->prevWayPointIndex << " " << initialOutgoing->distanceSquare << " " << isArea[initialOutgoing->coastline] << std::endl;
#endif

        groundTile.points.push_back(initialOutgoing->point);


        IntersectionPtr incoming=GetPreviousIntersection(intersectionsPathOrder[initialOutgoing->coastline],
                                                         initialOutgoing);

        if (incoming==NULL) {
#if defined(DEBUG_COASTLINE)
          std::cerr << "Polygon is not closed, but cannot find incoming" << std::endl;
#endif

          continue;
        }

#if defined(DEBUG_COASTLINE)
        std::cout << "Incoming: " << incoming->coastline << " " << incoming->prevWayPointIndex << " " << incoming->distanceSquare << std::endl;
#endif

        if (incoming->direction!=1) {
#if defined(DEBUG_COASTLINE)
          std::cerr << "The intersection before the outgoing intersection is not incoming as expected" << std::endl;
#endif

          continue;
        }

        WalkPathBack(groundTile,
                     initialOutgoing,
                     incoming,
                     points[initialOutgoing->coastline],
                     isArea[initialOutgoing->coastline]);

        IntersectionPtr nextCWIter=GetNextCW(intersectionsCW,
                                             incoming);

        bool error=false;

        while (!error &&
               nextCWIter!=initialOutgoing) {
#if defined(DEBUG_COASTLINE)
          std::cout << "Next CW: " << nextCWIter->coastline << " " << nextCWIter->prevWayPointIndex << " " << nextCWIter->distanceSquare << std::endl;
#endif

          IntersectionPtr outgoing=nextCWIter;

#if defined(DEBUG_COASTLINE)
          std::cout << "Outgoing: " << outgoing->coastline << " " << outgoing->prevWayPointIndex << " " << outgoing->distanceSquare << std::endl;
#endif

          if (outgoing->direction!=-1) {
#if defined(DEBUG_COASTLINE)
            std::cerr << "We expect an outgoing intersection" << std::endl;
#endif

            error=true;
            continue;
          }

          intersectionsOuter.remove(outgoing);

          WalkBorderCW(groundTile,
                       incoming,
                       outgoing,
                       borderPoints);

          incoming=GetPreviousIntersection(intersectionsPathOrder[outgoing->coastline],
                                           outgoing);

          if (incoming==NULL) {
#if defined(DEBUG_COASTLINE)
            std::cerr << "Polygon is not closed, but there are no intersections left" << std::endl;
#endif

            error=true;
            continue;
          }

#if defined(DEBUG_COASTLINE)
          std::cout << "Incoming: " << incoming->coastline << " " << incoming->prevWayPointIndex << " " << incoming->distanceSquare << std::endl;
#endif

          if (incoming->direction!=1) {
#if defined(DEBUG_COASTLINE)
            std::cerr << "We expect an incoming intersection" << std::endl;
#endif

            error=true;
            continue;
          }

          WalkPathBack(groundTile,
                       outgoing,
                       incoming,
                       points[outgoing->coastline],
                       isArea[outgoing->coastline]);

          nextCWIter=GetNextCW(intersectionsCW,
                               incoming);
        }

        if (error) {
          continue;
        }

        if (!groundTile.points.empty()) {
#if defined(DEBUG_COASTLINE)
        std::cout << "Polygon closed!" << std::endl;
#endif

          WalkBorderCW(groundTile,
                       incoming,
                       initialOutgoing,
                       borderPoints);

          cellGroundTileMap[cell->first].push_back(groundTile);
        }
      }
    }
  }

  std::string WaterIndexGenerator::GetDescription() const
  {
    return "Generate 'water.idx'";
  }

  bool WaterIndexGenerator::Import(const ImportParameter& parameter,
                                   Progress& progress,
                                   const TypeConfig& typeConfig)
  {
    FileScanner        scanner;

    uint32_t           minLonDat;
    uint32_t           minLatDat;
    uint32_t           maxLonDat;
    uint32_t           maxLatDat;

    std::vector<Level> levels;

    // Calculate size of tile cells for the maximum zoom level
    double             cellWidth;
    double             cellHeight;

    //
    // Read bounding box
    //

    if (!scanner.Open(AppendFileToDir(parameter.GetDestinationDirectory(),
                                      "bounding.dat"))) {
      progress.Error("Cannot open 'bounding.dat'");
      return false;
    }

    scanner.ReadNumber(minLatDat);
    scanner.ReadNumber(minLonDat);
    scanner.ReadNumber(maxLatDat);
    scanner.ReadNumber(maxLonDat);

    if (scanner.HasError() || !scanner.Close()) {
      progress.Error("Error while reading/closing 'bounding.dat'");
      return false;
    }

    //
    // Initialize levels
    //

    levels.resize(parameter.GetWaterIndexMaxMag()-parameter.GetWaterIndexMinMag()+1);

    cellWidth=360.0;
    cellHeight=180.0;

    for (size_t level=0; level<=parameter.GetWaterIndexMaxMag(); level++) {
      if (level>=parameter.GetWaterIndexMinMag() &&
          level<=parameter.GetWaterIndexMaxMag()) {
        levels[level-parameter.GetWaterIndexMinMag()].SetBox(minLatDat,maxLatDat,
                                                             minLonDat,maxLonDat,
                                                             cellWidth,cellHeight);
      }

      cellWidth=cellWidth/2;
      cellHeight=cellHeight/2;
    }

    //
    // Load and merge coastlines
    //

    LoadCoastlines(parameter,
                   progress,
                   typeConfig);

    MergeCoastlines(progress);


    progress.SetAction("Writing 'water.idx'");

    FileWriter writer;

    if (!writer.Open(AppendFileToDir(parameter.GetDestinationDirectory(),
                                     "water.idx"))) {
      progress.Error("Error while opening 'water.idx' for writing");
      return false;
    }

    DumpIndexHeader(parameter,
                    writer,
                    levels);

    for (size_t level=0; level<levels.size(); level++) {
      FileOffset         indexOffset;
      MercatorProjection projection;

      projection.Set(0,0,pow(2.0,(double)(level+parameter.GetWaterIndexMinMag())),640,480);

      progress.SetAction("Building tiles for level "+NumberToString(level+parameter.GetWaterIndexMinMag()));

      MarkCoastlineCells(progress,
                         levels[level]);

      ScanCellsHorizontally(progress,
                            levels[level]);

      ScanCellsVertically(progress,
                          levels[level]);

      if (parameter.GetAssumeLand()) {
        AssumeLand(parameter,
                   progress,
                   typeConfig,
                   levels[level]);
      }

      FillLand(progress,
               levels[level]);

      FillWater(progress,
                levels[level],20);

      writer.GetPos(indexOffset);
      writer.SetPos(levels[level].indexEntryOffset);
      writer.WriteFileOffset(indexOffset);
      writer.SetPos(indexOffset);

      std::map<Coord,std::list<GroundTile> > cellGroundTileMap;

      HandleAreaCoastlinesCompletelyInACell(parameter,
                                            progress,
                                            projection,
                                            levels[level],
                                            coastlines,
                                            cellGroundTileMap);

      HandleCoastlinesPartiallyInACell(parameter,
                                       progress,
                                       projection,
                                       levels[level],
                                       coastlines,
                                       cellGroundTileMap);

      for (size_t y=0; y<levels[level].cellYCount; y++) {
        for (size_t x=0; x<levels[level].cellXCount; x++) {
          State state=levels[level].GetState(x,y);

          writer.WriteFileOffset((FileOffset)state);
        }
      }

      for (std::map<Coord,std::list<GroundTile> >::const_iterator coord=cellGroundTileMap.begin();
          coord!=cellGroundTileMap.end();
          ++coord) {
        FileOffset startPos;

        writer.GetPos(startPos);

        writer.WriteNumber(coord->second.size());

        for (std::list<GroundTile>::const_iterator tile=coord->second.begin();
            tile!=coord->second.end();
            ++tile) {
          writer.Write((uint8_t)tile->type);

          writer.WriteNumber(tile->points.size());
          for (size_t t=0; t<tile->points.size(); t++) {
            writer.WriteNumber(tile->points[t].GetId());
            writer.WriteNumber((uint32_t)floor((tile->points[t].GetLat()+90.0)*conversionFactor+0.5));
            writer.WriteNumber((uint32_t)floor((tile->points[t].GetLon()+180.0)*conversionFactor+0.5));

          }
        }

        FileOffset endPos;
        uint32_t   cellId=coord->first.y*levels[level].cellXCount+coord->first.x;
        uint32_t   index=cellId*8;

        writer.GetPos(endPos);

        writer.SetPos(indexOffset+index);
        writer.WriteFileOffset(startPos);

        writer.SetPos(endPos);
      }
    }

    coastlines.clear();

    if (writer.HasError() || !writer.Close()) {
      progress.Error("Error while closing 'water.idx'");
      return false;
    }

    return true;
  }
}
