/*
    Copyright 2021 codenocold codenocold@qq.com
    Address : https://github.com/codenocold/ctm
    This file is part of the ctm firmware.
    The ctm firmware is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
    The ctm firmware is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef __STATUS_LED_H__
#define __STATUS_LED_H__

#ifndef LED_ACT_SET
#define LED_ACT_SET()   ((void) 0)
#endif

#ifndef LED_ACT_RESET
#define LED_ACT_RESET() ((void) 0)
#endif

#ifndef LED_ACT_GET
#define LED_ACT_GET()   (0U)
#endif

#endif /* __STATUS_LED_H__ */
