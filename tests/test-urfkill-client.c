#include <time.h>
#include <urfkill.h>
#include <stdio.h>
#include <glib.h>

static GMainLoop *loop = NULL;

static void
print_urf_device (UrfDevice *device)
{
	gint index, type;
	gboolean soft, hard;
	char *name;

	g_object_get (device,
		      "index", &index,
		      "type", &type,
		      "soft", &soft,
		      "hard", &hard,
		      "name", &name,
		      NULL);

	printf ("index = %u\n", index);
	printf ("type  = %u\n", type);
	printf ("soft  = %d\n", soft);
	printf ("hard  = %d\n", hard);
	printf ("name  = %s\n", name);
}

static void
device_added_cb (UrfClient *client, UrfDevice *device, gpointer data)
{
	printf ("== Added ==\n");
	print_urf_device (device);
	printf ("\n");
}

static void
device_removed_cb (UrfClient *client, UrfDevice *device, gpointer data)
{
	printf ("== removed ==\n");
	print_urf_device (device);
	printf ("\n");
}

static void
device_changed_cb (UrfClient *client, UrfDevice *device, gpointer data)
{
	printf ("== Changed ==\n");
	print_urf_device (device);
	printf ("\n");
}

static void
main_sigint_handler (gint sig)
{
	signal (SIGINT, SIG_DFL);
	g_main_loop_quit (loop);
}

int
main ()
{
	UrfClient *client = NULL;
	gboolean status;
	GList *devices, *item;
	UrfDevice *device;

#if !GLIB_CHECK_VERSION(2,36,0)
	g_type_init();
#endif

	client = urf_client_new ();
	urf_client_enumerate_devices_sync (client, NULL, NULL);

	g_signal_connect (client, "device-added", G_CALLBACK (device_added_cb), NULL);
	g_signal_connect (client, "device-removed", G_CALLBACK (device_removed_cb), NULL);
	g_signal_connect (client, "device-changed", G_CALLBACK (device_changed_cb), NULL);

	status = urf_client_set_wlan_block (client, TRUE);
	printf ("Status of block: %d\n", status);

	sleep (2);

	status = urf_client_set_wlan_block (client, FALSE);
	printf ("Status of unblock: %d\n", status);

	g_object_get (client, "key-control", &status, NULL);
	printf ("Key control is %s\n", status?"on":"off");

	status = urf_client_is_inhibited (client, NULL);
	printf ("Key control is %s\n", status?"inhibitied":"uninhibited");

	sleep (2);

	devices = urf_client_get_devices (client);

	for (item = devices; item; item = item->next) {
		device = (UrfDevice *)item->data;
		print_urf_device (device);
		printf ("\n");
	}

	loop = g_main_loop_new (NULL, FALSE);

	signal (SIGINT, main_sigint_handler);

	g_main_loop_run (loop);

	g_object_unref (client);

	return 0;
}
