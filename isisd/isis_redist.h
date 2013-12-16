#ifndef ISIS_REDIST_H
#define ISIS_REDIST_H

#ifdef HAVE_IPV6
#define REDIST_PROTOCOL_COUNT 2
#else
#define REDIST_PROTOCOL_COUNT 1
#endif

#define DEFAULT_ROUTE ZEBRA_ROUTE_MAX
#define DEFAULT_ORIGINATE 1
#define DEFAULT_ORIGINATE_ALWAYS 2

struct isis_ext_info
{
  int origin;
  u_char distance;
  uint32_t metric;
};

struct isis_redist
{
  int redist;
  uint32_t metric;
  char *map_name;
  struct route_map *map;
};

struct isis_area;
struct prefix;

struct route_table *get_ext_reach(struct isis_area *area,
                                  int family, int level);
void isis_redist_add(int type, struct prefix *p,
                     u_char distance, uint32_t metric);
void isis_redist_delete(int type, struct prefix *p);
int isis_redist_config_write(struct vty *vty, struct isis_area *area,
                             int family);
void isis_redist_init(void);

#endif
