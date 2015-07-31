/****************************************************************************

 Copyright (C) 2003 Lubos Lunak        <l.lunak@kde.org>

Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and associated documentation files (the "Software"),
to deal in the Software without restriction, including without limitation
the rights to use, copy, modify, merge, publish, distribute, sublicense,
and/or sell copies of the Software, and to permit persons to whom the
Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.

****************************************************************************/

#include "kselectionwatcher.h"

#include <config-kwindowsystem.h>

#include <QtCore/QObject>
#include <QGuiApplication>
#include <QAbstractNativeEventFilter>

#include <qx11info_x11.h>

static xcb_window_t get_selection_owner(xcb_connection_t *c, xcb_atom_t selection)
{
    xcb_window_t owner = XCB_NONE;
    xcb_get_selection_owner_reply_t *reply = xcb_get_selection_owner_reply(c, xcb_get_selection_owner(c, selection), 0);

    if (reply) {
        owner = reply->owner;
        free(reply);
    }

    return owner;
}

static xcb_atom_t intern_atom(xcb_connection_t *c, const char *name)
{
    xcb_atom_t atom = XCB_NONE;
    xcb_intern_atom_reply_t *reply = xcb_intern_atom_reply(c, xcb_intern_atom(c, false, strlen(name), name), 0);

    if (reply) {
        atom = reply->atom;
        free(reply);
    }

    return atom;
}

//*******************************************
// KSelectionWatcher
//*******************************************

class KSelectionWatcher::Private
    : public QAbstractNativeEventFilter
{
public:
    Private(KSelectionWatcher *watcher_P, xcb_atom_t selection_P, xcb_connection_t *c, xcb_window_t root)
        : connection(c),
          root(root),
          selection(selection_P),
          selection_owner(XCB_NONE),
          watcher(watcher_P)
    {
        QCoreApplication::instance()->installNativeEventFilter(this);
    }

    xcb_connection_t *connection;
    xcb_window_t root;
    const xcb_atom_t selection;
    xcb_window_t selection_owner;
    static xcb_atom_t manager_atom;

    static Private *create(KSelectionWatcher *watcher, xcb_atom_t selection_P, int screen_P);
    static Private *create(KSelectionWatcher *watcher, const char *selection_P, int screen_P);
    static Private *create(KSelectionWatcher *watcher, xcb_atom_t selection_P, xcb_connection_t *c, xcb_window_t root);
    static Private *create(KSelectionWatcher *watcher, const char *selection_P, xcb_connection_t *c, xcb_window_t root);

protected:
    bool nativeEventFilter(const QByteArray &eventType, void *message, long *result) Q_DECL_OVERRIDE {
        Q_UNUSED(result);
        if (eventType != "xcb_generic_event_t")
        {
            return false;
        }
        watcher->filterEvent(message);
        return false;
    }

private:
    KSelectionWatcher *watcher;
};

KSelectionWatcher::Private *KSelectionWatcher::Private::create(KSelectionWatcher *watcher, xcb_atom_t selection_P, int screen_P)
{
    if (QGuiApplication::platformName() == QStringLiteral("xcb")) {
        return create(watcher, selection_P, QX11Info::connection(), QX11Info::appRootWindow(screen_P));
    }
    return Q_NULLPTR;
}

KSelectionWatcher::Private *KSelectionWatcher::Private::create(KSelectionWatcher *watcher, xcb_atom_t selection_P, xcb_connection_t *c, xcb_window_t root)
{
    return new Private(watcher, selection_P, c, root);
}

KSelectionWatcher::Private *KSelectionWatcher::Private::create(KSelectionWatcher *watcher, const char *selection_P, int screen_P)
{
    if (QGuiApplication::platformName() == QStringLiteral("xcb")) {
        return create(watcher, selection_P, QX11Info::connection(), QX11Info::appRootWindow(screen_P));
    }
    return Q_NULLPTR;
}

KSelectionWatcher::Private *KSelectionWatcher::Private::create(KSelectionWatcher *watcher, const char *selection_P, xcb_connection_t *c, xcb_window_t root)
{
    return new Private(watcher, intern_atom(c, selection_P), c, root);
}

KSelectionWatcher::KSelectionWatcher(xcb_atom_t selection_P, int screen_P, QObject *parent_P)
    :   QObject(parent_P),
        d(Private::create(this, selection_P, screen_P))
{
    init();
}

KSelectionWatcher::KSelectionWatcher(const char *selection_P, int screen_P, QObject *parent_P)
    :   QObject(parent_P),
        d(Private::create(this, selection_P, screen_P))
{
    init();
}

KSelectionWatcher::KSelectionWatcher(xcb_atom_t selection, xcb_connection_t *c, xcb_window_t root, QObject *parent)
    : QObject(parent)
    , d(Private::create(this, selection, c, root))
{
    init();
}

KSelectionWatcher::KSelectionWatcher(const char *selection, xcb_connection_t *c, xcb_window_t root, QObject *parent)
    : QObject(parent)
    , d(Private::create(this, selection, c, root))
{
    init();
}

KSelectionWatcher::~KSelectionWatcher()
{
    delete d;
}

void KSelectionWatcher::init()
{
    if (!d) {
        return;
    }
    if (Private::manager_atom == XCB_NONE) {
        xcb_connection_t *c = d->connection;

        xcb_intern_atom_cookie_t atom_cookie = xcb_intern_atom(c, false, strlen("MANAGER"), "MANAGER");
        xcb_get_window_attributes_cookie_t attr_cookie = xcb_get_window_attributes(c, d->root);

        xcb_intern_atom_reply_t *atom_reply = xcb_intern_atom_reply(c, atom_cookie, 0);
        Private::manager_atom = atom_reply->atom;
        free(atom_reply);

        xcb_get_window_attributes_reply_t *attr = xcb_get_window_attributes_reply(c, attr_cookie, 0);
        uint32_t event_mask = attr->your_event_mask;
        free(attr);

        if (!(event_mask & XCB_EVENT_MASK_STRUCTURE_NOTIFY)) {
            // We need XCB_EVENT_MASK_STRUCTURE_NORITY on the root window
            event_mask |= XCB_EVENT_MASK_STRUCTURE_NOTIFY;
            xcb_change_window_attributes(c, d->root, XCB_CW_EVENT_MASK, &event_mask);
        }
    }

    owner(); // trigger reading of current selection status
}

xcb_window_t KSelectionWatcher::owner()
{
    if (!d) {
        return XCB_WINDOW_NONE;
    }
    xcb_connection_t *c = d->connection;

    xcb_window_t current_owner = get_selection_owner(c, d->selection);
    if (current_owner == XCB_NONE) {
        return XCB_NONE;
    }

    if (current_owner == d->selection_owner) {
        return d->selection_owner;
    }

    // We have a new selection owner - select for structure notify events
    uint32_t mask = XCB_EVENT_MASK_STRUCTURE_NOTIFY;
    xcb_void_cookie_t cookie = xcb_change_window_attributes_checked(c, current_owner, XCB_CW_EVENT_MASK, &mask);

    // Verify that the owner didn't change again while selecting for events
    xcb_window_t new_owner = get_selection_owner(c, d->selection);
    xcb_generic_error_t *err = xcb_request_check(c, cookie);

    if (!err && current_owner == new_owner) {
        d->selection_owner = current_owner;
        emit newOwner(d->selection_owner);
    } else {
        // ### This doesn't look right - the selection could have an owner
        d->selection_owner = XCB_NONE;
    }

    if (err) {
        free(err);
    }

    return d->selection_owner;
}

void KSelectionWatcher::filterEvent(void *ev_P)
{
    if (!d) {
        return;
    }
    xcb_generic_event_t *event = reinterpret_cast<xcb_generic_event_t *>(ev_P);
    const uint response_type = event->response_type & ~0x80;
    if (response_type == XCB_CLIENT_MESSAGE) {
        xcb_client_message_event_t *cm_event = reinterpret_cast<xcb_client_message_event_t *>(event);

        if (cm_event->type != Private::manager_atom || cm_event->data.data32[ 1 ] != d->selection) {
            return;
        }
        // owner() checks whether the owner changed and emits newOwner()
        owner();
        return;
    }
    if (response_type == XCB_DESTROY_NOTIFY) {
        xcb_destroy_notify_event_t *ev = reinterpret_cast<xcb_destroy_notify_event_t *>(event);
        if (d->selection_owner == XCB_NONE || ev->window != d->selection_owner) {
            return;
        }

        d->selection_owner = XCB_NONE; // in case the exactly same ID gets reused as the owner

        if (owner() == XCB_NONE) {
            emit lostOwner();    // it must be safe to delete 'this' in a slot
        }
        return;
    }
}

xcb_atom_t KSelectionWatcher::Private::manager_atom = XCB_NONE;