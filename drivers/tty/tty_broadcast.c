/*
 * Tty broadcast module implementation.
 */

#include <linux/tty.h>
#include <linux/tty_flip.h>

static LIST_HEAD(broadcast_tty_list);
static DEFINE_SPINLOCK(broadcast_tty_lock);

/* init/main.c */
extern char *saved_command_line;

/* exported functions */
void tty_broadcast_add(struct tty_struct *tty)
{
	if (strstr(saved_command_line, tty->name)) {
		spin_lock(&broadcast_tty_lock);
		list_add_tail(&tty->broadcast_node, &broadcast_tty_list);
		spin_unlock(&broadcast_tty_lock);
	}
}

void tty_broadcast_del(struct tty_struct *tty)
{
	if (strstr(saved_command_line, tty->name)) {
		spin_lock(&broadcast_tty_lock);
		list_del(&tty->broadcast_node);
		spin_unlock(&broadcast_tty_lock);
	}
}

void tty_broadcast_flip_buffer_push(void)
{
	struct tty_struct *tty;

	spin_lock(&broadcast_tty_lock);
	if (!list_empty(&broadcast_tty_list)) {
		list_for_each_entry(tty, &broadcast_tty_list, broadcast_node) {
			tty_flip_buffer_push(tty->port);
		}
	}
	spin_unlock(&broadcast_tty_lock);
}
EXPORT_SYMBOL(tty_broadcast_flip_buffer_push);

void tty_broadcast_push_char(unsigned char ch)
{
	struct tty_struct *tty;

	spin_lock(&broadcast_tty_lock);
	if (!list_empty(&broadcast_tty_list)) {
		list_for_each_entry(tty, &broadcast_tty_list, broadcast_node) {
			tty_insert_flip_char(tty->port, ch, TTY_NORMAL);
		}
	}
	spin_unlock(&broadcast_tty_lock);
}
EXPORT_SYMBOL(tty_broadcast_push_char);

