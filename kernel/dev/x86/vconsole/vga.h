/*-
 * SPDX-License-Identifier: Zlib
 *
 * Copyright (c) 2009-2018 Rink Springer <rink@rink.nu>
 * For conditions of distribution and use, see LICENSE file
 */
#ifndef ANANAS_VCONSOLE_VGA_H
#define ANANAS_VCONSOLE_VGA_H

#include "ivideo.h"

class VGA : public IVideo
{
  public:
    VGA();
    virtual ~VGA() = default;

    int GetHeight() override;
    int GetWidth() override;

    void SetCursor(VTTY&, int x, int y) override;
    void PutPixel(VTTY&, int x, int y, const Pixel& pixel) override;

    Result IOControl(Process* proc, unsigned long req, void* buffer[]) override;
    Result
    DetermineDevicePhysicalAddres(addr_t& physAddress, size_t& length, int& mapFlags) override;

  private:
    void WriteCRTC(uint8_t reg, uint8_t val);

    uint32_t vga_io;
    volatile uint16_t* vga_video_mem;
};

#endif /* ANANAS_VCONSOLE_VGA_H */
