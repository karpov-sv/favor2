from django.template.response import TemplateResponse

def error403(request):
    return TemplateResponse(request, 'errors/403.html')

def error404(request):
    return TemplateResponse(request, 'errors/404.html')
