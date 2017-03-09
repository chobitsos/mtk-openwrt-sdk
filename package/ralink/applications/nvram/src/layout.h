/******************************************************************
 * $File:   layout.h
 *
 * $Author: Hua Shao
 * $Date:   Oct, 2014
 *
 ******************************************************************/

#ifndef LAYOUT_H
#define LAYOUT_H

/* uncomment NVRAM_CUSTOM_LAYOUT to enable your layout. */

//#define NVRAM_CUSTOM_LAYOUT

#ifdef NVRAM_CUSTOM_LAYOUT
static layout_t nvlayout[] =
{
    /* the first 4KB of "Config" is reserved for uboot. */
    {
        .name = "uboot",
        .offset = 0x0,
        .size = 0x1000,
        .flag = NV_RO,
    },
    /* you can use the rest space, up to 60KB. */
    {
        .name = "custom",
        .offset = 0x1000,
            .size = 0x1000,
    },
};
#endif /* NVRAM_CUSTOM_LAYOUT */

#endif /* LAYOUT_H */


