#include "viewer.h"
#include "magnifying_glass.h"
#include "configuration.h"
#include "magnifying_glass.h"
#include "goto_flow.h"
#include "goto_flow_gl.h"
#include "bookmarks_dialog.h"
#include "render.h"
#include "goto_dialog.h"
#include "translator.h"
#include "onstart_flow_selection_dialog.h"
#include "page_label_widget.h"
#include "notifications_label_widget.h"

#include <QFile>
#define STEPS 22



Viewer::Viewer(QWidget * parent)
:QScrollArea(parent),
currentPage(0),
magnifyingGlassShowed(false),
fullscreen(false),
information(false),
adjustToWidthRatio(1),
doublePage(false),
wheelStop(false),
direction(1),
restoreMagnifyingGlass(false),
drag(false)

{
	translator = new YACReaderTranslator(this);
	translator->hide();
	translatorAnimation = new QPropertyAnimation(translator,"pos");
	translatorAnimation->setDuration(150);
	translatorXPos = -10000;
	translator->move(-translator->width(),10);
	//current comic page
	content = new QLabel(this);
	configureContent(tr("Press 'O' to open comic."));
	//scroll area configuration
	setBackgroundRole(QPalette::Dark);
	setWidget(content);
	setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	setFrameStyle(QFrame::NoFrame);
	setAlignment(Qt::AlignCenter);

	QPalette palette;
	palette.setColor(backgroundRole(), Configuration::getConfiguration().getBackgroundColor());
	setPalette(palette);
	//---------------------------------------
	mglass = new MagnifyingGlass(Configuration::getConfiguration().getMagnifyingGlassSize(),this);
	mglass->hide();
	content->setMouseTracking(true);
	setMouseTracking(true);

	showCursor();

	goToDialog = new GoToDialog(this);
	
	QSettings * settings = new QSettings(QCoreApplication::applicationDirPath()+"/YACReader.ini",QSettings::IniFormat);

	//CONFIG GOTO_FLOW--------------------------------------------------------
	if(QGLFormat::hasOpenGL() && !settings->contains(USE_OPEN_GL))
	{
		OnStartFlowSelectionDialog * flowSelDialog = new OnStartFlowSelectionDialog();

		flowSelDialog->exec();
		if(flowSelDialog->result() == QDialog::Accepted)
			settings->setValue(USE_OPEN_GL,2);
		else
			settings->setValue(USE_OPEN_GL,0);

		delete flowSelDialog;
	}

	if(QGLFormat::hasOpenGL() && (settings->value(USE_OPEN_GL).toBool() == true))
		goToFlow = new GoToFlowGL(this,Configuration::getConfiguration().getFlowType());
	else
		goToFlow = new GoToFlow(this,Configuration::getConfiguration().getFlowType());

	goToFlow->hide();
	showGoToFlowAnimation = new QPropertyAnimation(goToFlow,"pos");
	showGoToFlowAnimation->setDuration(150);
	
	bd = new BookmarksDialog(this->parentWidget());
	
	render = new Render();

	hideCursorTimer = new QTimer();
	hideCursorTimer->setSingleShot(true); 

	if(Configuration::getConfiguration().getDoublePage())
		doublePageSwitch();

	createConnections();

	hideCursorTimer->start(2500);

	setMouseTracking(true);

	//animations
	verticalScroller = new QPropertyAnimation(verticalScrollBar(), "sliderPosition");
	connect(verticalScroller,SIGNAL(valueChanged (const QVariant &)),this,SIGNAL(backgroundChanges()));

	notificationsLabel = new NotificationsLabelWidget(this);
	notificationsLabel->hide();

	informationLabel = new PageLabelWidget(this);

}

void Viewer::createConnections()
{
	//magnifyingGlass (update mg after a background change
	connect(this,SIGNAL(backgroundChanges()),mglass,SLOT(updateImage()));

	//goToDialog
	connect(goToDialog,SIGNAL(goToPage(unsigned int)),this,SLOT(goTo(unsigned int)));

	//goToFlow goTo
	connect(goToFlow,SIGNAL(goToPage(unsigned int)),this,SLOT(goTo(unsigned int)));

	//current time
	QTimer * t = new QTimer();
	connect(t,SIGNAL(timeout()),this,SLOT(updateInformation()));
	t->start(1000);

	//hide cursor
	connect(hideCursorTimer,SIGNAL(timeout()),this,SLOT(hideCursor()));

	//bookmarks
	connect(bd,SIGNAL(goToPage(unsigned int)),this,SLOT(goTo(unsigned int)));

	//render
	connect(render,SIGNAL(errorOpening()),this,SLOT(resetContent()));
	connect(render,SIGNAL(errorOpening()),this,SLOT(showMessageErrorOpening()));
	connect(render,SIGNAL(numPages(unsigned int)),goToFlow,SLOT(setNumSlides(unsigned int)));
	connect(render,SIGNAL(numPages(unsigned int)),goToDialog,SLOT(setNumPages(unsigned int)));
	//connect(render,SIGNAL(numPages(unsigned int)),this,SLOT(updateInformation()));
	connect(render,SIGNAL(imageLoaded(int,QByteArray)),goToFlow,SLOT(setImageReady(int,QByteArray)));
	connect(render,SIGNAL(currentPageReady()),this,SLOT(updatePage()));
	connect(render,SIGNAL(processingPage()),this,SLOT(setLoadingMessage()));
	connect(render,SIGNAL(currentPageIsBookmark(bool)),this,SIGNAL(pageIsBookmark(bool)));
	connect(render,SIGNAL(pageChanged(int)),this,SLOT(updateInformation()));
	//connect(render,SIGNAL(bookmarksLoaded(Bookmarks)),this,SLOT(setBookmarks(Bookmarks)));

	connect(render,SIGNAL(isLast()),this,SLOT(showIsLastMessage()));
	connect(render,SIGNAL(isCover()),this,SLOT(showIsCoverMessage()));

	connect(render,SIGNAL(bookmarksUpdated()),this,SLOT(setBookmarks()));
}

void Viewer::open(QString pathFile)
{
	if(render->hasLoadedComic())
		save();
	//bd->setBookmarks(*bm);

	goToFlow->reset();
	render->load(pathFile);
	//render->update();

	verticalScrollBar()->setSliderPosition(verticalScrollBar()->minimum());
	
	if(Configuration::getConfiguration().getShowInformation() && !information)
	{
		QTimer * timer = new QTimer();
		connect(timer,SIGNAL(timeout()),this,SLOT(informationSwitch()));
		connect(timer,SIGNAL(timeout()),timer,SLOT(deleteLater()));
		timer->start();
	}

	informationLabel->setText("...");

	setAcceptDrops(true);
}

void Viewer::showMessageErrorOpening()
{
	QMessageBox::critical(NULL,tr("Not found"),tr("Comic not found"));
}

void Viewer::next()
{
	direction = 1;
	render->nextPage();
	updateInformation();
}

void Viewer::prev()
{
	direction = -1;
	render->previousPage();
	updateInformation();
}
void Viewer::showGoToDialog()
{
	goToDialog->show();
}
void Viewer::goTo(unsigned int page)
{
	direction = 1; //in "go to" direction is always fordward
	render->goTo(page);
}

void Viewer::updatePage()
{
	QPixmap * previousPage = currentPage;
	currentPage = render->getCurrentPage();
	content->setPixmap(*currentPage);
	updateContentSize();
	updateVerticalScrollBar();
	emit backgroundChanges();
	emit(pageAvailable(true));
	//TODO -> update bookmark action
	setFocus(Qt::ShortcutFocusReason);
	delete previousPage;
	if(restoreMagnifyingGlass)
	{
		restoreMagnifyingGlass = false;
		showMagnifyingGlass();
	}

}

void Viewer::updateContentSize()
{
	//there is an image to resize
	if(currentPage !=0 && !currentPage->isNull())
	{
		if(Configuration::getConfiguration().getAdjustToFullSize())
		{
			content->resize(currentPage->width(),currentPage->height());
		}
		else
		{
			float aspectRatio = (float)currentPage->width()/currentPage->height();
			//Fit to width
			if(Configuration::getConfiguration().getAdjustToWidth())
			{
				adjustToWidthRatio = Configuration::getConfiguration().getFitToWidthRatio();
				if(static_cast<int>(width()*adjustToWidthRatio/aspectRatio)<height())
					if(static_cast<int>(height()*aspectRatio)>width())
						content->resize(width(),static_cast<int>(width()/aspectRatio));
					else
						content->resize(static_cast<int>(height()*aspectRatio),height());
				else
					content->resize(width()*adjustToWidthRatio,static_cast<int>(width()*adjustToWidthRatio/aspectRatio));
			}
			//Fit to height or fullsize/custom size
			else
			{
				if(static_cast<int>(height()*aspectRatio)>width()) //page width exceeds window width
					content->resize(width(),static_cast<int>(width()/aspectRatio));
				else
					content->resize(static_cast<int>(height()*aspectRatio),height());
			}
		}
		emit backgroundChanges();
	}
	content->update(); //TODO, it shouldn't be neccesary
}

void Viewer::updateVerticalScrollBar()
{
	if(direction > 0)
		verticalScrollBar()->setSliderPosition(verticalScrollBar()->minimum());
	else
		verticalScrollBar()->setSliderPosition(verticalScrollBar()->maximum());
}

void Viewer::scrollDown()
{
	if(verticalScrollBar()->sliderPosition()==verticalScrollBar()->maximum())
	{
		next();
	}
	else
	{
		int currentPos = verticalScrollBar()->sliderPosition();
		verticalScroller->setDuration(250);
		verticalScroller->setStartValue(currentPos);
		verticalScroller->setEndValue(nextPos);

		verticalScroller->start();

		emit backgroundChanges();
	}
}

void Viewer::scrollUp()
{
	if(verticalScrollBar()->sliderPosition()==verticalScrollBar()->minimum())
	{
		prev();
	}
	else
	{
		int currentPos = verticalScrollBar()->sliderPosition();
		verticalScroller->setDuration(250);
		verticalScroller->setStartValue(currentPos);
		verticalScroller->setEndValue(nextPos);

		verticalScroller->start();

		emit backgroundChanges();
	}
}

void Viewer::keyPressEvent(QKeyEvent *event)
{
	if(render->hasLoadedComic())
	{
		if(goToFlow->isVisible() && event->key()!=Qt::Key_S)
			QCoreApplication::sendEvent(goToFlow,event);
		else
			switch (event->key())
		{
			case Qt::Key_Space:
				posByStep = height()/STEPS;
				nextPos=verticalScrollBar()->sliderPosition()+static_cast<int>((height()*0.80));
				scrollDown();
				break;
			case Qt::Key_B:
				posByStep = height()/STEPS;
				nextPos=verticalScrollBar()->sliderPosition()-static_cast<int>((height()*0.80));
				scrollUp();
				break;
			case Qt::Key_S:
				goToFlowSwitch();
				break;
			case Qt::Key_T:
				translatorSwitch();
				break;
			case Qt::Key_Down:
				/*if(verticalScrollBar()->sliderPosition()==verticalScrollBar()->maximum())
				next();
				else*/
				QAbstractScrollArea::keyPressEvent(event);
				emit backgroundChanges();
				break;
			case Qt::Key_Up:
				/*if(verticalScrollBar()->sliderPosition()==verticalScrollBar()->minimum())
				prev();
				else*/
				QAbstractScrollArea::keyPressEvent(event);
				emit backgroundChanges();
				break;
			case Qt::Key_Home:
				goTo(0);
				break;
			case Qt::Key_End:
				goTo(this->render->numPages()-1);
				break;
			default:
				QAbstractScrollArea::keyPressEvent(event);
				break;
		}
		if(mglass->isVisible())
			switch(event->key())
		{
			case Qt::Key_Plus: case Qt::Key_Minus: case Qt::Key_Underscore: case Qt::Key_Asterisk:
				QCoreApplication::sendEvent(mglass,event);
		}
	}
}

void Viewer::wheelEvent(QWheelEvent * event)
{
	if(render->hasLoadedComic())
	{
		if((event->delta()<0)&&(verticalScrollBar()->sliderPosition()==verticalScrollBar()->maximum()))
		{
			if(wheelStop)
			{
				next();
				verticalScroller->stop();
				event->accept();
				wheelStop = false;
				return;
			}
			else
				wheelStop = true;
		}
		else
		{
			if((event->delta()>0)&&(verticalScrollBar()->sliderPosition()==verticalScrollBar()->minimum()))
			{
				if(wheelStop)
				{
					prev();
					verticalScroller->stop();
					event->accept();
					wheelStop = false;
					return;
				}
				else
					wheelStop = true;
			}
		}

		int deltaNotFinished = 0;
		if(verticalScroller->state() == QAbstractAnimation::Running)
		{
			deltaNotFinished = verticalScroller->startValue().toInt() - verticalScroller->endValue().toInt();
			verticalScroller->stop();
		}


		int currentPos = verticalScrollBar()->sliderPosition();
		verticalScroller->setDuration(250);
		verticalScroller->setStartValue(currentPos);
		verticalScroller->setEndValue(currentPos - event->delta() - deltaNotFinished);

		verticalScroller->start();

		//QAbstractScrollArea::wheelEvent(event);
	}
}

void Viewer::resizeEvent(QResizeEvent * event)
{
	updateContentSize();
	goToFlow->move(QPoint((width()-goToFlow->width())/2,height()-goToFlow->height()));
	informationLabel->updatePosition();
	QScrollArea::resizeEvent(event);
}

void Viewer::mouseMoveEvent(QMouseEvent * event)
{
	showCursor();
	hideCursorTimer->start(2500);

	if(magnifyingGlassShowed)
		mglass->move(static_cast<int>(event->x()-float(mglass->width())/2),static_cast<int>(event->y()-float(mglass->height())/2));

	if(render->hasLoadedComic())
	{
		if(showGoToFlowAnimation->state()!=QPropertyAnimation::Running)
		{
		if(goToFlow->isVisible())
		{
			animateHideGoToFlow();
			//goToFlow->hide();
		}
		else
		{
			int umbral = (width()-goToFlow->width())/2;
			if((event->y()>height()-15)&&(event->x()>umbral)&&(event->x()<width()-umbral))
			{

				animateShowGoToFlow();
				hideCursorTimer->stop();
			}
		}
		}

		if(drag)
		{
			int currentPosY = verticalScrollBar()->sliderPosition();
			int currentPosX = horizontalScrollBar()->sliderPosition();
			verticalScrollBar()->setSliderPosition(currentPosY=currentPosY+(yDragOrigin-event->y()));
			horizontalScrollBar()->setSliderPosition(currentPosX=currentPosX+(xDragOrigin-event->x()));
			yDragOrigin = event->y();
			xDragOrigin = event->x();
		}
	}


}

const QPixmap * Viewer::pixmap()
{
	return content->pixmap();
}

void Viewer::magnifyingGlassSwitch()
{
	magnifyingGlassShowed?hideMagnifyingGlass():showMagnifyingGlass();
}

void Viewer::showMagnifyingGlass()
{
	if(render->hasLoadedComic())
	{
		QPoint p = QPoint(cursor().pos().x(),cursor().pos().y());
		p = this->parentWidget()->mapFromGlobal(p);
		mglass->move(static_cast<int>(p.x()-float(mglass->width())/2)
			,static_cast<int>(p.y()-float(mglass->height())/2));
		mglass->show();
		mglass->updateImage(mglass->x()+mglass->width()/2,mglass->y()+mglass->height()/2);
		magnifyingGlassShowed = true;
	}
}

void Viewer::hideMagnifyingGlass()
{
	mglass->hide();
	magnifyingGlassShowed = false;
}

void Viewer::informationSwitch()
{
	information?informationLabel->hide():informationLabel->show();
	//informationLabel->move(QPoint((width()-informationLabel->width())/2,0));
	information=!information;
	Configuration::getConfiguration().setShowInformation(information);
	//TODO it shouldn't be neccesary
	informationLabel->adjustSize();
	informationLabel->update();
}

void Viewer::updateInformation()
{
	if(render->hasLoadedComic())
	{
		informationLabel->setText(render->getCurrentPagesInformation()+" - "+QTime::currentTime().toString("HH:mm"));
		informationLabel->adjustSize();
		informationLabel->update(); //TODO it shouldn't be neccesary
	}
}

void Viewer::goToFlowSwitch()
{
	goToFlow->isVisible()?animateHideGoToFlow():showGoToFlow();
}

void Viewer::translatorSwitch()
{
	translator->isVisible()?animateHideTranslator():animateShowTranslator();
}

void Viewer::showGoToFlow()
{
	if(render->hasLoadedComic())
	{
		animateShowGoToFlow();
	}
}

void Viewer::animateShowGoToFlow()
{
	if(goToFlow->isHidden() && showGoToFlowAnimation->state()!=QPropertyAnimation::Running)
	{
		disconnect(showGoToFlowAnimation,SIGNAL(finished()),goToFlow,SLOT(hide()));
		connect(showGoToFlowAnimation,SIGNAL(finished()),this,SLOT(moveCursoToGoToFlow()));
		showGoToFlowAnimation->setStartValue(QPoint((width()-goToFlow->width())/2,height()-10));
		showGoToFlowAnimation->setEndValue(QPoint((width()-goToFlow->width())/2,height()-goToFlow->height()));
		showGoToFlowAnimation->start();
		goToFlow->centerSlide(render->getIndex());
		goToFlow->setPageNumber(render->getIndex());
		goToFlow->show();
		goToFlow->setFocus(Qt::OtherFocusReason);
	}
}

void Viewer::animateHideGoToFlow()
{
	if(goToFlow->isVisible() && showGoToFlowAnimation->state()!=QPropertyAnimation::Running)
	{
		connect(showGoToFlowAnimation,SIGNAL(finished()),goToFlow,SLOT(hide()));
		disconnect(showGoToFlowAnimation,SIGNAL(finished()),this,SLOT(moveCursoToGoToFlow()));
		showGoToFlowAnimation->setStartValue(QPoint((width()-goToFlow->width())/2,height()-goToFlow->height()));
		showGoToFlowAnimation->setEndValue(QPoint((width()-goToFlow->width())/2,height()));
		showGoToFlowAnimation->start();
		goToFlow->centerSlide(render->getIndex());
		goToFlow->setPageNumber(render->getIndex());
		this->setFocus(Qt::OtherFocusReason);
	}
}

void Viewer::moveCursoToGoToFlow()
{
		//Move cursor to goToFlow widget on show (this avoid hide when mouse is moved)
		int y = goToFlow->pos().y();
		int x1 = goToFlow->pos().x();
		int x2 = x1 + goToFlow->width();
		QPoint cursorPos = mapFromGlobal(cursor().pos());
		int cursorX = cursorPos.x();
		int cursorY = cursorPos.y();

		if(cursorY <= y)
			cursorY = y + 10;
		if(cursorX <= x1)
			cursorX = x1 + 10;
		if(cursorX >= x2)
			cursorX = x2 - 10;
		cursor().setPos(mapToGlobal(QPoint(cursorX,cursorY)));
		hideCursorTimer->stop();
		showCursor();
}

void Viewer::rotateLeft()
{
	render->rotateLeft();
}
void Viewer::rotateRight()
{
	render->rotateRight();
}

//TODO
void Viewer::setBookmark(bool set)
{
    render->setBookmark();
    if(set) //add bookmark
    {
	render->setBookmark();
    }
    else //remove bookmark
    {
	render->removeBookmark();
    }
}

void Viewer::save ()
{
    if(render->hasLoadedComic())
	render->save();
}

void Viewer::doublePageSwitch()
{
    doublePage = !doublePage;
    render->doublePageSwitch();
	Configuration::getConfiguration().setDoublePage(doublePage);
}

void Viewer::resetContent()
{
    configureContent(tr("Press 'O' to open comic."));
	goToFlow->reset();
    emit reset();
}

void Viewer::setLoadingMessage()
{
	if(magnifyingGlassShowed)
	{
		hideMagnifyingGlass();
		restoreMagnifyingGlass = true;
	}
	emit(pageAvailable(false));
	configureContent(tr("Loading...please wait!"));
}

void Viewer::configureContent(QString msg)
{
    content->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    content->setScaledContents(true);
    content->setAlignment(Qt::AlignTop|Qt::AlignHCenter);
    content->setText(msg);
    content->setFont(QFont("courier new", 12));
    content->adjustSize();
	setFocus(Qt::ShortcutFocusReason);
	//emit showingText();
}

void Viewer::hideCursor()
{
#ifdef Q_OS_MAC
	setCursor(QCursor(QBitmap(1,1),QBitmap(1,1)));
#else
	setCursor(Qt::BlankCursor);
#endif
}
void Viewer::showCursor()
{
	if(drag)
		setCursor(Qt::ClosedHandCursor);
	else
		setCursor(Qt::OpenHandCursor);
}

void Viewer::updateOptions()
{
	
	goToFlow->setFlowType(Configuration::getConfiguration().getFlowType());
	updateBackgroundColor(Configuration::getConfiguration().getBackgroundColor());
	updateContentSize();
	//goToFlow->updateSize();
}

void Viewer::updateBackgroundColor(const QColor & color)
{
	QPalette palette;
	palette.setColor(backgroundRole(), color);
	setPalette(palette);
}

void Viewer::animateShowTranslator()
{
	if(translator->isHidden() && translatorAnimation->state()!=QPropertyAnimation::Running)
	{
		disconnect(translatorAnimation,SIGNAL(finished()),translator,SLOT(hide()));
		if(translatorXPos == -10000)
			translatorXPos = (width()-translator->width())/2;
		int x = qMax(0,qMin(translatorXPos,width()-translator->width()));
		if(translator->pos().x()<0)
		{
			translatorAnimation->setStartValue(QPoint(-translator->width(),translator->pos().y()));
		}
		else
		{
			translatorAnimation->setStartValue(QPoint(width()+translator->width(),translator->pos().y()));
		}
		translatorAnimation->setEndValue(QPoint(x,translator->pos().y()));
		translatorAnimation->start();
		translator->show();
		translator->setFocus(Qt::OtherFocusReason);
	}
}
void Viewer::animateHideTranslator()
{
	if(translator->isVisible() && translatorAnimation->state()!=QPropertyAnimation::Running)
	{
		connect(translatorAnimation,SIGNAL(finished()),translator,SLOT(hide()));
		translatorAnimation->setStartValue(QPoint(translatorXPos = translator->pos().x(),translator->pos().y()));
		if((translator->width()/2)+translator->pos().x() <= width()/2)
			translatorAnimation->setEndValue(QPoint(-translator->width(),translator->pos().y()));
		else
			translatorAnimation->setEndValue(QPoint(width()+translator->width(),translator->pos().y()));
		translatorAnimation->start();
		this->setFocus(Qt::OtherFocusReason);
	}
}

void Viewer::mousePressEvent ( QMouseEvent * event )
{
	drag = true;
	yDragOrigin = event->y();
	xDragOrigin = event->x();
	setCursor(Qt::ClosedHandCursor);
}
void Viewer::mouseReleaseEvent ( QMouseEvent * event )
{
	drag = false;
	setCursor(Qt::OpenHandCursor);
}

void Viewer::updateFitToWidthRatio(float ratio)
{
	adjustToWidthRatio = ratio;
	updateContentSize();
}

void Viewer::updateConfig(QSettings * settings)
{
	goToFlow->updateConfig(settings);

	QPalette palette;
	palette.setColor(backgroundRole(), Configuration::getConfiguration().getBackgroundColor());
	setPalette(palette);

	
}

void Viewer::updateImageOptions()
{
	render->reload();
}

void Viewer::setBookmarks()
{
	bd->setBookmarks(*render->getBookmarks());
}

void Viewer::showIsCoverMessage()
{
	notificationsLabel->setText(tr("Cover!"));
	notificationsLabel->flash();

}
		
void Viewer::showIsLastMessage()
{
	notificationsLabel->setText(tr("Last page!"));
	notificationsLabel->flash();
}

unsigned int Viewer::getIndex()
{
	return render->getIndex()+1;
}