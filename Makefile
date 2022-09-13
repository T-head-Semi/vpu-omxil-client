##############################################################################
# Copyright 2021-2022 Alibaba Group Holding Limited
# 
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
# 
#     http://www.apache.org/licenses/LICENSE-2.0
# 
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
##############################################################################

INSTALL_DIR = output

export M32 = -m64
CFLAGS += -DADDRESS_WIDTH_64
CFLAGS += -g -O -fno-inline -W -Wall -pthread
CFLAGS += -I. \
	-Iomxil \
	-I$(INC_PATH)/omxil \
	-I$(INC_PATH)/bellagio \
	-I$(INC_PATH)/plink

BELLAGIO_LIB ?= $(SYSROOT_DIR)/usr/lib/libomxil-bellagio.so.0

base_SRCS = OSAL.c

omxenc_HDRS = omxencparameters.h omxtestcommon.h
omxenc_SRCS = omxencparameters.c omxenctest.c omxtestcommon.c
omxenc_OBJS = $(base_SRCS:.c=.o) $(omxenc_SRCS:.c=.o)

all: omxenctest install

clean:
	rm -f $(omxenc_OBJS) omxenctest
	rm -rf $(INSTALL_DIR)

install: omxenctest
	$(shell if [ ! -e $(INSTALL_DIR) ];then mkdir -p $(INSTALL_DIR); fi)
	cp -vf omxenctest $(INSTALL_DIR)

omxenctest: $(omxenc_OBJS)
	$(CC) -o omxenctest $(omxenc_OBJS) $(BELLAGIO_LIB) -L$(LIB_PATH)/plink -lplink -ldl -lpthread

%.o : %.c
	$(CC) $(CFLAGS) -c $< -o $@


