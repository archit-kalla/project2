/*
 * Please do not edit this file.
 * It was generated using rpcgen.
 */

#include "communicate.h"

bool_t
xdr_Article_t (XDR *xdrs, Article_t *objp)
{
	register int32_t *buf;

	int i;
	 if (!xdr_vector (xdrs, (char *)objp->text, 120,
		sizeof (char), (xdrproc_t) xdr_char))
		 return FALSE;
	 if (!xdr_int (xdrs, &objp->seqnum))
		 return FALSE;
	 if (!xdr_int (xdrs, &objp->reply_seqnum))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_Page_t (XDR *xdrs, Page_t *objp)
{
	register int32_t *buf;

	int i;
	 if (!xdr_vector (xdrs, (char *)objp->articles, 10,
		sizeof (Article_t), (xdrproc_t) xdr_Article_t))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_Written_seqnums_t (XDR *xdrs, Written_seqnums_t *objp)
{
	register int32_t *buf;

	int i;

	if (xdrs->x_op == XDR_ENCODE) {
		buf = XDR_INLINE (xdrs, ( 50 ) * BYTES_PER_XDR_UNIT);
		if (buf == NULL) {
			 if (!xdr_vector (xdrs, (char *)objp->seqnums, 50,
				sizeof (int), (xdrproc_t) xdr_int))
				 return FALSE;
		} else {
			{
				register int *genp;

				for (i = 0, genp = objp->seqnums;
					i < 50; ++i) {
					IXDR_PUT_LONG(buf, *genp++);
				}
			}
		}
		return TRUE;
	} else if (xdrs->x_op == XDR_DECODE) {
		buf = XDR_INLINE (xdrs, ( 50 ) * BYTES_PER_XDR_UNIT);
		if (buf == NULL) {
			 if (!xdr_vector (xdrs, (char *)objp->seqnums, 50,
				sizeof (int), (xdrproc_t) xdr_int))
				 return FALSE;
		} else {
			{
				register int *genp;

				for (i = 0, genp = objp->seqnums;
					i < 50; ++i) {
					*genp++ = IXDR_GET_LONG(buf);
				}
			}
		}
	 return TRUE;
	}

	 if (!xdr_vector (xdrs, (char *)objp->seqnums, 50,
		sizeof (int), (xdrproc_t) xdr_int))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_read_1_argument (XDR *xdrs, read_1_argument *objp)
{
	 if (!xdr_int (xdrs, &objp->Page_num))
		 return FALSE;
	 if (!xdr_int (xdrs, &objp->Nr))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_write_1_argument (XDR *xdrs, write_1_argument *objp)
{
	 if (!xdr_Article_t (xdrs, &objp->Article))
		 return FALSE;
	 if (!xdr_int (xdrs, &objp->Nw))
		 return FALSE;
	 if (!xdr_string (xdrs, &objp->sender_ip, ~0))
		 return FALSE;
	 if (!xdr_string (xdrs, &objp->sender_port, ~0))
		 return FALSE;
	return TRUE;
}